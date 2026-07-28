#include "tinypy/vm.h"

#include "internal.h"

#include <string.h>
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
#include <limits.h>
#include <stdio.h>
#endif
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
typedef struct tinypy_debug_location_t {
    struct tinypy_debug_location_t *next;
    size_t allocation_size;
    size_t filename_size;
    size_t function_size;
    int32_t line_number;
    char text[];
} tinypy_debug_location_t;
typedef enum tinypy_cycle_diagnostics_edge_kind_e {
    TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM = 0,
    TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_KEY = 1,
    TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_VALUE = 2,
    TINYPY_CYCLE_DIAGNOSTICS_EDGE_CELL_CONTENT = 3
} tinypy_cycle_diagnostics_edge_kind_e;
typedef struct tinypy_cycle_diagnostics_edge_t {
    struct tinypy_cycle_diagnostics_edge_t *next;
    tinypy_value_t *target;
    tinypy_value_t *key;
    const tinypy_debug_location_t *assigned_at;
    size_t index;
    tinypy_cycle_diagnostics_edge_kind_e kind;
} tinypy_cycle_diagnostics_edge_t;
typedef struct tinypy_cycle_diagnostics_value_t {
    struct tinypy_cycle_diagnostics_value_t *previous;
    struct tinypy_cycle_diagnostics_value_t *next;
    struct tinypy_cycle_diagnostics_value_t *bucket_next;
    tinypy_cycle_diagnostics_edge_t *edges;
    tinypy_value_t *value;
    const tinypy_debug_location_t *created_at;
    tinypy_bool_t reachable;
} tinypy_cycle_diagnostics_value_t;
struct tinypy_cycle_diagnostics_state_t {
    tinypy_cycle_diagnostics_value_t *values;
    tinypy_cycle_diagnostics_value_t **buckets;
    tinypy_debug_location_t *locations;
    size_t bucket_count;
    size_t value_count;
};
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_cycle_diagnostics_pointer_hash(const tinypy_value_t *value) {
    uintptr_t bits = (uintptr_t)value;

    bits >>= 3U;
    bits ^= bits >> 17U;
    bits *= (uintptr_t)UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11U;
    return (size_t)bits;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_text_view(const tinypy_value_t *value, const char **out_text, size_t *out_size) {
    if (value != NULL && (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE)) {
        *out_text = (const char *)TINYPY_TEXT_BYTES(value);
        *out_size = TINYPY_TEXT_BYTE_SIZE(value);
        return;
    }
    *out_text = "<unknown>";
    *out_size = sizeof("<unknown>") - 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_debug_location_t *__tinypy_debug_location_current(tinypy_vm_t *vm) {
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    const char *filename = "<native>";
    const char *function = "<native>";
    size_t filename_size = sizeof("<native>") - 1U;
    size_t function_size = sizeof("<native>") - 1U;
    int32_t line_number = INT32_C(0);
    tinypy_debug_location_t *location;
    size_t text_size;
    size_t allocation_size;

    if (vm->current_frame != NULL && vm->current_frame->code != NULL) {
        tinypy_value_t *code = vm->current_frame->code;

        __tinypy_debug_text_view(TINYPY_CODE_FILENAME(code), &filename, &filename_size);
        __tinypy_debug_text_view(TINYPY_CODE_NAME(code), &function, &function_size);
        line_number = tinypy_frame_line_number(&vm->current_frame->base.base);
    }
    for (location = state->locations; location != NULL; location = location->next) {
        const char *stored_filename = location->text;
        const char *stored_function = stored_filename + location->filename_size + 1U;

        if (location->line_number == line_number &&
            location->filename_size == filename_size &&
            location->function_size == function_size &&
            (filename_size == 0U || memcmp(stored_filename, filename, filename_size) == 0) &&
            (function_size == 0U || memcmp(stored_function, function, function_size) == 0)) {
            return location;
        }
    }
    text_size = filename_size + 1U + function_size + 1U;
    allocation_size = offsetof(tinypy_debug_location_t, text) + text_size;
    location = (tinypy_debug_location_t *)tinypy_internal_vm_allocate(
        vm,
        allocation_size);
    location->next = state->locations;
    location->allocation_size = allocation_size;
    location->filename_size = filename_size;
    location->function_size = function_size;
    location->line_number = line_number;
    if (filename_size != 0U) {
        (void)memcpy(location->text, filename, filename_size);
    }
    location->text[filename_size] = '\0';
    if (function_size != 0U) {
        (void)memcpy(location->text + filename_size + 1U, function, function_size);
    }
    location->text[filename_size + 1U + function_size] = '\0';
    state->locations = location;
    return location;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cycle_diagnostics_values_reserve(tinypy_vm_t *vm) {
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    tinypy_cycle_diagnostics_value_t **buckets;
    tinypy_cycle_diagnostics_value_t *record;
    size_t bucket_count;

    if (state->bucket_count != 0U && state->value_count < state->bucket_count - state->bucket_count / 4U) {
        return;
    }
    bucket_count = state->bucket_count != 0U ? state->bucket_count * 2U : 64U;
    buckets = (tinypy_cycle_diagnostics_value_t **)tinypy_internal_vm_allocate(
        vm,
        bucket_count * sizeof(*buckets));
    (void)memset(buckets, 0, bucket_count * sizeof(*buckets));
    for (record = state->values; record != NULL; record = record->next) {
        size_t bucket = __tinypy_cycle_diagnostics_pointer_hash(record->value) & (bucket_count - 1U);

        record->bucket_next = buckets[bucket];
        buckets[bucket] = record;
    }
    if (state->buckets != NULL) {
        tinypy_internal_vm_deallocate(
            vm,
            state->buckets,
            state->bucket_count * sizeof(*state->buckets));
    }
    state->buckets = buckets;
    state->bucket_count = bucket_count;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cycle_diagnostics_value_t *__tinypy_cycle_diagnostics_value_find(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    const tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    tinypy_cycle_diagnostics_value_t *record;
    size_t bucket;

    bucket = __tinypy_cycle_diagnostics_pointer_hash(value) & (state->bucket_count - 1U);
    for (record = state->buckets[bucket]; record != NULL; record = record->bucket_next) {
        if (record->value == value) {
            return record;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cycle_diagnostics_mark_reachable(tinypy_value_t *value, void *user_data) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, value);

    if (record != NULL) {
        record->reachable = TINYPY_TRUE;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cycle_diagnostics_edge_destroy(tinypy_vm_t *vm, tinypy_cycle_diagnostics_value_t *record, tinypy_cycle_diagnostics_edge_t *edge) {
    tinypy_cycle_diagnostics_edge_t **link = &record->edges;

    while (*link != edge) {
        link = &(*link)->next;
    }
    *link = edge->next;
    tinypy_internal_vm_deallocate(
        vm,
        edge,
        sizeof(*edge));
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cycle_diagnostics_edges_clear(tinypy_vm_t *vm, tinypy_cycle_diagnostics_value_t *record) {
    while (record->edges != NULL) {
        __tinypy_cycle_diagnostics_edge_destroy(vm, record, record->edges);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cycle_diagnostics_edge_t *__tinypy_cycle_diagnostics_edge_new(
    tinypy_vm_t *vm,
    tinypy_cycle_diagnostics_value_t *record,
    tinypy_cycle_diagnostics_edge_kind_e kind,
    size_t index,
    tinypy_value_t *key,
    tinypy_value_t *target,
    const tinypy_debug_location_t *assigned_at) {
    tinypy_cycle_diagnostics_edge_t *edge = (tinypy_cycle_diagnostics_edge_t *)tinypy_internal_vm_allocate(
        vm,
        sizeof(*edge));

    edge->next = record->edges;
    edge->target = target;
    edge->key = key;
    edge->assigned_at = assigned_at;
    edge->index = index;
    edge->kind = kind;
    record->edges = edge;
    return edge;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cycle_diagnostics_edge_t *__tinypy_cycle_diagnostics_list_edge_find(tinypy_cycle_diagnostics_value_t *record, size_t index) {
    tinypy_cycle_diagnostics_edge_t *edge;

    for (edge = record->edges; edge != NULL; edge = edge->next) {
        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM && edge->index == index) {
            return edge;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cycle_diagnostics_edge_t *__tinypy_cycle_diagnostics_dict_edge_find(
    tinypy_cycle_diagnostics_value_t *record,
    tinypy_cycle_diagnostics_edge_kind_e kind,
    const tinypy_value_t *key) {
    tinypy_cycle_diagnostics_edge_t *edge;

    for (edge = record->edges; edge != NULL; edge = edge->next) {
        if (edge->kind == kind && edge->key == key) {
            return edge;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cycle_diagnostics_edge_t *__tinypy_cycle_diagnostics_cell_edge_find(tinypy_cycle_diagnostics_value_t *record) {
    tinypy_cycle_diagnostics_edge_t *edge;

    for (edge = record->edges; edge != NULL; edge = edge->next) {
        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_CELL_CONTENT) {
            return edge;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_value_register_enabled(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    tinypy_cycle_diagnostics_value_t *record;
    size_t bucket;

    __tinypy_cycle_diagnostics_values_reserve(vm);
    record = (tinypy_cycle_diagnostics_value_t *)tinypy_internal_vm_allocate(
        vm,
        sizeof(*record));
    (void)memset(record, 0, sizeof(*record));
    record->value = value;
    record->created_at = __tinypy_debug_location_current(vm);
    record->next = state->values;
    if (state->values != NULL) {
        state->values->previous = record;
    }
    state->values = record;
    bucket = __tinypy_cycle_diagnostics_pointer_hash(value) & (state->bucket_count - 1U);
    record->bucket_next = state->buckets[bucket];
    state->buckets[bucket] = record;
    state->value_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_value_reuse_enabled(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_cycle_diagnostics_value_t *record;

    record = __tinypy_cycle_diagnostics_value_find(vm, value);
    __tinypy_cycle_diagnostics_edges_clear(vm, record);
    record->created_at = __tinypy_debug_location_current(vm);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_value_unregister_enabled(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    tinypy_cycle_diagnostics_value_t *record;
    tinypy_cycle_diagnostics_value_t **bucket_link;
    size_t bucket;

    record = __tinypy_cycle_diagnostics_value_find(vm, value);
    __tinypy_cycle_diagnostics_edges_clear(vm, record);
    if (record->previous != NULL) {
        record->previous->next = record->next;
    }
    else {
        state->values = record->next;
    }
    if (record->next != NULL) {
        record->next->previous = record->previous;
    }
    bucket = __tinypy_cycle_diagnostics_pointer_hash(value) & (state->bucket_count - 1U);
    bucket_link = &state->buckets[bucket];
    while (*bucket_link != record) {
        bucket_link = &(*bucket_link)->bucket_next;
    }
    *bucket_link = record->bucket_next;
    state->value_count -= 1U;
    tinypy_internal_vm_deallocate(
        vm,
        record,
        sizeof(*record));
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_list_extend_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *const *items, size_t item_count) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, list);
    const tinypy_debug_location_t *location = __tinypy_debug_location_current(vm);
    size_t item_index;

    if (record == NULL) {
        return;
    }
    for (item_index = 0U; item_index < item_count; ++item_index) {
        (void)__tinypy_cycle_diagnostics_edge_new(
            vm,
            record,
            TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM,
            index + item_index,
            NULL,
            items[item_index],
            location);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_list_insert_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, list);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    for (edge = record->edges; edge != NULL; edge = edge->next) {
        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM && edge->index >= index) {
            edge->index += 1U;
        }
    }
    (void)__tinypy_cycle_diagnostics_edge_new(
        vm,
        record,
        TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM,
        index,
        NULL,
        item,
        __tinypy_debug_location_current(vm));
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_list_set_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, list);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    edge = __tinypy_cycle_diagnostics_list_edge_find(record, index);
    edge->target = item;
    edge->assigned_at = __tinypy_debug_location_current(vm);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_list_remove_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, list);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    edge = __tinypy_cycle_diagnostics_list_edge_find(record, index);
    __tinypy_cycle_diagnostics_edge_destroy(vm, record, edge);
    for (edge = record->edges; edge != NULL; edge = edge->next) {
        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM && edge->index > index) {
            edge->index -= 1U;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_list_clear_enabled(tinypy_vm_t *vm, tinypy_value_t *list) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, list);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    edge = record->edges;
    while (edge != NULL) {
        tinypy_cycle_diagnostics_edge_t *next = edge->next;

        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM) {
            __tinypy_cycle_diagnostics_edge_destroy(vm, record, edge);
        }
        edge = next;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_list_reindex_enabled(tinypy_vm_t *vm, tinypy_value_t *list) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, list);
    tinypy_cycle_diagnostics_edge_t *edge;
    size_t index;

    if (record == NULL) {
        return;
    }
    for (edge = record->edges; edge != NULL; edge = edge->next) {
        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM) {
            edge->index = SIZE_MAX;
        }
    }
    for (index = 0U; index < TINYPY_LIST_SIZE(list); ++index) {
        tinypy_value_t *item = TINYPY_LIST_GET(list, index);

        for (edge = record->edges; edge != NULL; edge = edge->next) {
            if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM && edge->index == SIZE_MAX && edge->target == item) {
                edge->index = index;
                break;
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_dict_set_enabled(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t inserted) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, dict);
    const tinypy_debug_location_t *location = __tinypy_debug_location_current(vm);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    if (inserted != 0) {
        (void)__tinypy_cycle_diagnostics_edge_new(
            vm,
            record,
            TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_KEY,
            0U,
            key,
            key,
            location);
    }
    edge = __tinypy_cycle_diagnostics_dict_edge_find(record, TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_VALUE, key);
    if (edge == NULL) {
        edge = __tinypy_cycle_diagnostics_edge_new(
            vm,
            record,
            TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_VALUE,
            0U,
            key,
            value,
            location);
    }
    else {
        edge->target = value;
        edge->assigned_at = location;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_dict_delete_enabled(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, dict);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    edge = __tinypy_cycle_diagnostics_dict_edge_find(record, TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_KEY, key);
    if (edge != NULL) {
        __tinypy_cycle_diagnostics_edge_destroy(vm, record, edge);
    }
    edge = __tinypy_cycle_diagnostics_dict_edge_find(record, TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_VALUE, key);
    if (edge != NULL) {
        __tinypy_cycle_diagnostics_edge_destroy(vm, record, edge);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_dict_clear_enabled(tinypy_vm_t *vm, tinypy_value_t *dict) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, dict);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    edge = record->edges;
    while (edge != NULL) {
        tinypy_cycle_diagnostics_edge_t *next = edge->next;

        if (edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_KEY || edge->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_VALUE) {
            __tinypy_cycle_diagnostics_edge_destroy(vm, record, edge);
        }
        edge = next;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_cell_set_enabled(tinypy_vm_t *vm, tinypy_value_t *cell, tinypy_value_t *content) {
    tinypy_cycle_diagnostics_value_t *record = __tinypy_cycle_diagnostics_value_find(vm, cell);
    tinypy_cycle_diagnostics_edge_t *edge;

    if (record == NULL) {
        return;
    }
    edge = __tinypy_cycle_diagnostics_cell_edge_find(record);
    if (content == NULL) {
        if (edge != NULL) {
            __tinypy_cycle_diagnostics_edge_destroy(vm, record, edge);
        }
        return;
    }
    if (edge == NULL) {
        (void)__tinypy_cycle_diagnostics_edge_new(
            vm,
            record,
            TINYPY_CYCLE_DIAGNOSTICS_EDGE_CELL_CONTENT,
            0U,
            NULL,
            content,
            __tinypy_debug_location_current(vm));
    }
    else {
        edge->target = content;
        edge->assigned_at = __tinypy_debug_location_current(vm);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_initialize(tinypy_vm_t *vm) {
    tinypy_cycle_diagnostics_state_t *state;

    state = (tinypy_cycle_diagnostics_state_t *)tinypy_internal_vm_allocate(
        vm,
        sizeof(*state));
    (void)memset(state, 0, sizeof(*state));
    vm->cycle_diagnostics = state;
    __tinypy_cycle_diagnostics_values_reserve(vm);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cycle_diagnostics_finalize(tinypy_vm_t *vm) {
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;

    if (state == NULL) {
        return;
    }
    while (state->values != NULL) {
        tinypy_internal_cycle_diagnostics_value_unregister_enabled(vm, state->values->value);
    }
    while (state->locations != NULL) {
        tinypy_debug_location_t *location = state->locations;

        state->locations = location->next;
        tinypy_internal_vm_deallocate(
            vm,
            location,
            location->allocation_size);
    }
    tinypy_internal_vm_deallocate(
        vm,
        state->buckets,
        state->bucket_count * sizeof(*state->buckets));
    vm->cycle_diagnostics = NULL;
    tinypy_internal_vm_deallocate(
        vm,
        state,
        sizeof(*state));
}
#endif
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
typedef struct tinypy_debug_cycle_node_t {
    tinypy_value_t *value;
    const tinypy_cycle_diagnostics_value_t *diagnostics;
    size_t edge_count;
    size_t component;
    tinypy_bool_t visited;
} tinypy_debug_cycle_node_t;
typedef struct tinypy_debug_cycle_edge_t {
    size_t target;
    const tinypy_cycle_diagnostics_edge_t *diagnostics;
} tinypy_debug_cycle_edge_t;
typedef struct tinypy_debug_cycle_graph_t {
    tinypy_vm_t *vm;
    tinypy_diagnostic_callback_t callback;
    void *user_data;
    tinypy_debug_cycle_node_t *nodes;
    size_t node_count;
    size_t *slots;
    size_t slot_capacity;
    size_t *offsets;
    tinypy_debug_cycle_edge_t *edges;
    size_t edge_count;
} tinypy_debug_cycle_graph_t;
typedef struct tinypy_debug_cycle_edge_context_t {
    tinypy_debug_cycle_graph_t *graph;
    tinypy_value_t *source;
    size_t cursor;
    size_t reference_index;
    tinypy_bool_t fill;
    tinypy_bool_t diagnostics_references;
} tinypy_debug_cycle_edge_context_t;
typedef struct tinypy_debug_cycle_dfs_frame_t {
    size_t node;
    size_t next_edge;
} tinypy_debug_cycle_dfs_frame_t;
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_debug_cycle_find(const tinypy_debug_cycle_graph_t *graph, const tinypy_value_t *value) {
    size_t slot;

    if (value == NULL || graph->slot_capacity == 0U) {
        return SIZE_MAX;
    }
    slot = __tinypy_cycle_diagnostics_pointer_hash(value) & (graph->slot_capacity - 1U);
    while (graph->slots[slot] != 0U) {
        size_t index = graph->slots[slot] - 1U;

        if (graph->nodes[index].value == value) {
            return index;
        }
        slot = (slot + 1U) & (graph->slot_capacity - 1U);
    }
    return SIZE_MAX;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_cycle_diagnostics_edge_t *__tinypy_debug_cycle_reference_diagnostics(
    tinypy_debug_cycle_edge_context_t *context,
    tinypy_value_t *target) {
    tinypy_cycle_diagnostics_value_t *record;
    tinypy_cycle_diagnostics_edge_t *edge = NULL;

    (void)target;
    if (context->diagnostics_references == 0) {
        return NULL;
    }
    record = __tinypy_cycle_diagnostics_value_find(context->graph->vm, context->source);
    switch (TINYPY_VALUE_KIND(context->source)) {
    case TINYPY_VALUE_LIST:
        edge = __tinypy_cycle_diagnostics_list_edge_find(record, context->reference_index);
        break;
    case TINYPY_VALUE_DICT: {
        tinypy_dict_entry_t *entry = TINYPY_DICT_ITERATOR_BEGIN(context->source);
        tinypy_dict_entry_t *entry_end = TINYPY_DICT_ITERATOR_END(context->source);
        size_t active_index = context->reference_index / 2U;
        size_t current_index = 0U;

        for (; entry != entry_end; ++entry) {
            if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) {
                continue;
            }
            if (current_index == active_index) {
                tinypy_cycle_diagnostics_edge_kind_e kind =
                    context->reference_index % 2U == 0U
                        ? TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_KEY
                        : TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_VALUE;

                edge = __tinypy_cycle_diagnostics_dict_edge_find(record, kind, entry->key);
                break;
            }
            current_index += 1U;
        }
        break;
    }
    case TINYPY_VALUE_CELL:
        edge = __tinypy_cycle_diagnostics_cell_edge_find(record);
        break;
    default:
        break;
    }
    return edge;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_cycle_edge_visit(tinypy_value_t *value, void *user_data) {
    tinypy_debug_cycle_edge_context_t *context = (tinypy_debug_cycle_edge_context_t *)user_data;
    const tinypy_cycle_diagnostics_edge_t *diagnostics =
        __tinypy_debug_cycle_reference_diagnostics(context, value);
    size_t target = __tinypy_debug_cycle_find(context->graph, value);

    if (context->diagnostics_references != 0) {
        context->reference_index += 1U;
    }
    if (target == SIZE_MAX) {
        return;
    }
    if (context->fill != 0) {
        context->graph->edges[context->cursor].target = target;
        context->graph->edges[context->cursor].diagnostics = diagnostics;
    }
    context->cursor += 1U;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_cycle_visit_owning_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_type_t *type = value->type;
    tinypy_debug_cycle_edge_context_t *context = (tinypy_debug_cycle_edge_context_t *)user_data;

    context->diagnostics_references = 0;
    visit(&type->base.base, user_data);
    if (type->release_references != NULL) {
        context->diagnostics_references = 1;
        context->reference_index = 0U;
        type->release_references(value, visit, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_debug_message_size(int32_t written, size_t capacity) {
    if (written <= 0) {
        return 0U;
    }
    return (size_t)written < capacity ? (size_t)written : capacity - 1U;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_debug_text_precision(size_t size) {
    return size <= (size_t)INT_MAX ? (int32_t)size : INT_MAX;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_emit(
    const tinypy_debug_cycle_graph_t *graph,
    const char *message,
    size_t message_size) {
    tinypy_diagnostic_t diagnostic;

    if (message_size == 0U || graph->callback == NULL) {
        return;
    }
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.abi_version = TINYPY_ABI_VERSION;
    diagnostic.struct_size = (uint32_t)sizeof(diagnostic);
    diagnostic.error_kind = TINYPY_ERROR_RUNTIME;
    diagnostic.message = message;
    diagnostic.message_size = message_size;
    graph->callback(graph->user_data, &diagnostic);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_emit_location(
    const tinypy_debug_cycle_graph_t *graph,
    const char *prefix,
    const tinypy_debug_location_t *location) {
    const char *filename;
    const char *function;
    char message[1024];
    int32_t written;

    if (location == NULL) {
        return;
    }
    filename = location->text;
    function = filename + location->filename_size + 1U;
    written = snprintf(
        message,
        sizeof(message),
        "%s%.*s:%d in %.*s",
        prefix,
        __tinypy_debug_text_precision(location->filename_size),
        filename,
        (int32_t)location->line_number,
        __tinypy_debug_text_precision(location->function_size),
        function);
    __tinypy_debug_emit(graph, message, __tinypy_debug_message_size(written, sizeof(message)));
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_emit_cycle_edge(
    tinypy_debug_cycle_graph_t *graph,
    size_t source,
    const tinypy_debug_cycle_edge_t *edge) {
    tinypy_vm_t *vm = graph->vm;
    const tinypy_cycle_diagnostics_edge_t *diagnostics = edge->diagnostics;
    char message[1024];
    int32_t written;

    if (diagnostics == NULL) {
        written = snprintf(
            message,
            sizeof(message),
            "    owning edge: object #%zu -> object #%zu",
            source + 1U,
            edge->target + 1U);
    }
    else if (diagnostics->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_LIST_ITEM) {
        written = snprintf(
            message,
            sizeof(message),
            "    owning edge: object #%zu[%zu] -> object #%zu",
            source + 1U,
            diagnostics->index,
            edge->target + 1U);
    }
    else if (diagnostics->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_CELL_CONTENT) {
        written = snprintf(
            message,
            sizeof(message),
            "    owning edge: object #%zu.cell_contents -> object #%zu",
            source + 1U,
            edge->target + 1U);
    }
    else {
        const tinypy_value_t *key = diagnostics->key;
        const char *key_prefix =
            diagnostics->kind == TINYPY_CYCLE_DIAGNOSTICS_EDGE_DICT_KEY
                ? "key "
                : "";

        if (key != NULL && (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE)) {
            const char *key_text = (const char *)TINYPY_TEXT_BYTES(key);
            size_t key_size = TINYPY_TEXT_BYTE_SIZE(key);

            written = snprintf(
                message,
                sizeof(message),
                "    owning edge: object #%zu[%s'%.*s'] -> object #%zu",
                source + 1U,
                key_prefix,
                __tinypy_debug_text_precision(key_size),
                key_text,
                edge->target + 1U);
        }
        else if (key != NULL && (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_BOOL)) {
            written = snprintf(
                message,
                sizeof(message),
                "    owning edge: object #%zu[%s%lld] -> object #%zu",
                source + 1U,
                key_prefix,
                (long long)TINYPY_INTEGER_VALUE(key),
                edge->target + 1U);
        }
        else {
            tinypy_type_t *key_type = key != NULL ? key->type : &vm->types[TINYPY_VALUE_NONE];

            written = snprintf(
                message,
                sizeof(message),
                "    owning edge: object #%zu[%s<%.*s>] -> object #%zu",
                source + 1U,
                key_prefix,
                __tinypy_debug_text_precision(key_type->name_size),
                key_type->name,
                edge->target + 1U);
        }
    }
    __tinypy_debug_emit(graph, message, __tinypy_debug_message_size(written, sizeof(message)));
    if (diagnostics != NULL) {
        __tinypy_debug_emit_location(
            graph,
            "      candidate break site at ",
            diagnostics->assigned_at);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_emit_cycle(
    tinypy_debug_cycle_graph_t *graph,
    size_t component,
    size_t component_size,
    size_t cycle_number) {
    char message[1024];
    int32_t written;
    size_t index;

    written = snprintf(
        message,
        sizeof(message),
        "[tinypy cycle] cycle %zu contains %zu unreachable object%s; break one owning edge listed below",
        cycle_number,
        component_size,
        component_size == 1U ? "" : "s");
    __tinypy_debug_emit(graph, message, __tinypy_debug_message_size(written, sizeof(message)));

    for (index = 0U; index < graph->node_count; ++index) {
        tinypy_debug_cycle_node_t *node = &graph->nodes[index];
        tinypy_value_t *value;
        tinypy_type_t *type;

        if (node->component != component) {
            continue;
        }
        value = node->value;
        type = value->type;
        written = snprintf(
            message,
            sizeof(message),
            "  object #%zu: %.*s, refcount=%td",
            index + 1U,
            __tinypy_debug_text_precision(type->name_size),
            type->name,
            value->ref);
        __tinypy_debug_emit(graph, message, __tinypy_debug_message_size(written, sizeof(message)));
        __tinypy_debug_emit_location(graph, "    created at ", node->diagnostics->created_at);
    }
    for (index = 0U; index < graph->node_count; ++index) {
        tinypy_debug_cycle_node_t *node = &graph->nodes[index];
        size_t edge;

        if (node->component != component) {
            continue;
        }
        for (edge = graph->offsets[index]; edge < graph->offsets[index + 1U]; ++edge) {
            const tinypy_debug_cycle_edge_t *cycle_edge = &graph->edges[edge];

            if (graph->nodes[cycle_edge->target].component != component) {
                continue;
            }
            __tinypy_debug_emit_cycle_edge(graph, index, cycle_edge);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_debug_cycle_graph_destroy(tinypy_debug_cycle_graph_t *graph) {
    if (graph->edges != NULL) {
        tinypy_internal_vm_deallocate(
            graph->vm,
            graph->edges,
            graph->edge_count * sizeof(*graph->edges));
    }
    if (graph->offsets != NULL) {
        tinypy_internal_vm_deallocate(
            graph->vm,
            graph->offsets,
            (graph->node_count + 1U) * sizeof(*graph->offsets));
    }
    if (graph->slots != NULL) {
        tinypy_internal_vm_deallocate(
            graph->vm,
            graph->slots,
            graph->slot_capacity * sizeof(*graph->slots));
    }
    if (graph->nodes != NULL) {
        tinypy_internal_vm_deallocate(
            graph->vm,
            graph->nodes,
            graph->node_count * sizeof(*graph->nodes));
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_debug_report_unreachable_cycles(
    tinypy_vm_t *vm,
    tinypy_diagnostic_callback_t callback,
    void *user_data) {
    tinypy_debug_cycle_graph_t graph;
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    tinypy_cycle_diagnostics_value_t *record;
    size_t index;
    size_t slot;
    size_t *reverse_offsets = NULL;
    size_t *reverse_edges = NULL;
    size_t *order = NULL;
    size_t *stack = NULL;
    size_t *component_sizes = NULL;
    tinypy_debug_cycle_dfs_frame_t *dfs = NULL;
    size_t order_count = 0U;
    size_t component_count = 0U;
    size_t cycle_count = 0U;

    if (state == NULL || callback == NULL) {
        return 0U;
    }
    (void)memset(&graph, 0, sizeof(graph));
    graph.vm = vm;
    graph.callback = callback;
    graph.user_data = user_data;
    for (record = state->values; record != NULL; record = record->next) {
        if (record->value->ref != 0 && record->reachable == TINYPY_FALSE) {
            graph.node_count += 1U;
        }
    }
    if (graph.node_count == 0U) {
        return 0U;
    }
    graph.nodes = (tinypy_debug_cycle_node_t *)tinypy_internal_vm_allocate(
        vm,
        graph.node_count * sizeof(*graph.nodes));
    (void)memset(graph.nodes, 0, graph.node_count * sizeof(*graph.nodes));

    graph.slot_capacity = 256U;
    while (graph.slot_capacity < graph.node_count * 2U) {
        graph.slot_capacity *= 2U;
    }
    graph.slots = (size_t *)tinypy_internal_vm_allocate(
        vm,
        graph.slot_capacity * sizeof(*graph.slots));
    (void)memset(graph.slots, 0, graph.slot_capacity * sizeof(*graph.slots));

    index = 0U;
    for (record = state->values; record != NULL; record = record->next) {
        tinypy_value_t *value = record->value;

        if (value->ref == 0 || record->reachable != TINYPY_FALSE) {
            continue;
        }
        graph.nodes[index].value = value;
        graph.nodes[index].diagnostics = record;
        graph.nodes[index].component = SIZE_MAX;
        slot = __tinypy_cycle_diagnostics_pointer_hash(value) & (graph.slot_capacity - 1U);
        while (graph.slots[slot] != 0U) {
            slot = (slot + 1U) & (graph.slot_capacity - 1U);
        }
        graph.slots[slot] = index + 1U;
        index += 1U;
    }

    graph.offsets = (size_t *)tinypy_internal_vm_allocate(
        vm,
        (graph.node_count + 1U) * sizeof(*graph.offsets));
    graph.offsets[0] = 0U;
    for (index = 0U; index < graph.node_count; ++index) {
        tinypy_debug_cycle_edge_context_t context;

        context.graph = &graph;
        context.source = graph.nodes[index].value;
        context.cursor = 0U;
        context.reference_index = 0U;
        context.fill = 0;
        context.diagnostics_references = 0;
        __tinypy_debug_cycle_visit_owning_references(
            graph.nodes[index].value,
            __tinypy_debug_cycle_edge_visit,
            &context);
        graph.nodes[index].edge_count = context.cursor;
        graph.offsets[index + 1U] = graph.offsets[index] + context.cursor;
    }
    graph.edge_count = graph.offsets[graph.node_count];
    if (graph.edge_count != 0U) {
        graph.edges = (tinypy_debug_cycle_edge_t *)tinypy_internal_vm_allocate(
            vm,
            graph.edge_count * sizeof(*graph.edges));
    }
    for (index = 0U; index < graph.node_count; ++index) {
        tinypy_debug_cycle_edge_context_t context;

        context.graph = &graph;
        context.source = graph.nodes[index].value;
        context.cursor = graph.offsets[index];
        context.reference_index = 0U;
        context.fill = 1;
        context.diagnostics_references = 0;
        __tinypy_debug_cycle_visit_owning_references(
            graph.nodes[index].value,
            __tinypy_debug_cycle_edge_visit,
            &context);
    }

    reverse_offsets = (size_t *)tinypy_internal_vm_allocate(
        vm,
        (graph.node_count + 1U) * sizeof(*reverse_offsets));
    (void)memset(reverse_offsets, 0, (graph.node_count + 1U) * sizeof(*reverse_offsets));
    for (index = 0U; index < graph.edge_count; ++index) {
        size_t target = graph.edges[index].target;

        reverse_offsets[target + 1U] += 1U;
    }
    for (index = 0U; index < graph.node_count; ++index) {
        reverse_offsets[index + 1U] += reverse_offsets[index];
        graph.nodes[index].edge_count = reverse_offsets[index];
    }
    if (graph.edge_count != 0U) {
        reverse_edges = (size_t *)tinypy_internal_vm_allocate(
            vm,
            graph.edge_count * sizeof(*reverse_edges));
    }
    for (index = 0U; index < graph.node_count; ++index) {
        size_t edge;

        for (edge = graph.offsets[index]; edge < graph.offsets[index + 1U]; ++edge) {
            size_t target = graph.edges[edge].target;
            size_t cursor = graph.nodes[target].edge_count;

            reverse_edges[cursor] = index;
            graph.nodes[target].edge_count += 1U;
        }
    }

    order = (size_t *)tinypy_internal_vm_allocate(
        vm,
        graph.node_count * sizeof(*order));
    stack = (size_t *)tinypy_internal_vm_allocate(
        vm,
        graph.node_count * sizeof(*stack));
    component_sizes = (size_t *)tinypy_internal_vm_allocate(
        vm,
        graph.node_count * sizeof(*component_sizes));
    (void)memset(component_sizes, 0, graph.node_count * sizeof(*component_sizes));
    dfs = (tinypy_debug_cycle_dfs_frame_t *)tinypy_internal_vm_allocate(
        vm,
        graph.node_count * sizeof(*dfs));

    for (index = 0U; index < graph.node_count; ++index) {
        size_t dfs_count;

        if (graph.nodes[index].visited != 0) {
            continue;
        }
        dfs_count = 1U;
        dfs[0].node = index;
        dfs[0].next_edge = graph.offsets[index];
        graph.nodes[index].visited = 1;
        while (dfs_count != 0U) {
            tinypy_debug_cycle_dfs_frame_t *frame = &dfs[dfs_count - 1U];
            size_t edge_end = graph.offsets[frame->node + 1U];

            if (frame->next_edge < edge_end) {
                size_t target = graph.edges[frame->next_edge].target;

                frame->next_edge += 1U;
                if (graph.nodes[target].visited == 0) {
                    graph.nodes[target].visited = 1;
                    dfs[dfs_count].node = target;
                    dfs[dfs_count].next_edge = graph.offsets[target];
                    dfs_count += 1U;
                }
                continue;
            }
            order[order_count] = frame->node;
            order_count += 1U;
            dfs_count -= 1U;
        }
    }

    for (index = graph.node_count; index != 0U; --index) {
        size_t root = order[index - 1U];
        size_t stack_count;

        if (graph.nodes[root].component != SIZE_MAX) {
            continue;
        }
        stack_count = 1U;
        stack[0] = root;
        graph.nodes[root].component = component_count;
        while (stack_count != 0U) {
            size_t node = stack[stack_count - 1U];
            size_t edge;

            stack_count -= 1U;
            component_sizes[component_count] += 1U;
            for (edge = reverse_offsets[node]; edge < reverse_offsets[node + 1U]; ++edge) {
                size_t source = reverse_edges[edge];

                if (graph.nodes[source].component == SIZE_MAX) {
                    graph.nodes[source].component = component_count;
                    stack[stack_count] = source;
                    stack_count += 1U;
                }
            }
        }
        component_count += 1U;
    }

    for (index = 0U; index < component_count; ++index) {
        int32_t cyclic = component_sizes[index] > 1U ? 1 : 0;
        size_t node;

        if (cyclic == 0) {
            for (node = 0U; node < graph.node_count; ++node) {
                size_t edge;

                if (graph.nodes[node].component != index) {
                    continue;
                }
                for (edge = graph.offsets[node]; edge < graph.offsets[node + 1U]; ++edge) {
                    if (graph.edges[edge].target == node) {
                        cyclic = 1;
                        break;
                    }
                }
                break;
            }
        }
        if (cyclic != 0) {
            cycle_count += 1U;
            __tinypy_debug_emit_cycle(
                &graph,
                index,
                component_sizes[index],
                cycle_count);
        }
    }

    tinypy_internal_vm_deallocate(
        vm,
        dfs,
        graph.node_count * sizeof(*dfs));
    tinypy_internal_vm_deallocate(
        vm,
        component_sizes,
        graph.node_count * sizeof(*component_sizes));
    tinypy_internal_vm_deallocate(
        vm,
        stack,
        graph.node_count * sizeof(*stack));
    tinypy_internal_vm_deallocate(
        vm,
        order,
        graph.node_count * sizeof(*order));
    if (reverse_edges != NULL) {
        tinypy_internal_vm_deallocate(
            vm,
            reverse_edges,
            graph.edge_count * sizeof(*reverse_edges));
    }
    tinypy_internal_vm_deallocate(
        vm,
        reverse_offsets,
        (graph.node_count + 1U) * sizeof(*reverse_offsets));
    __tinypy_debug_cycle_graph_destroy(&graph);
    return cycle_count;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_debug_report_cycles(
    tinypy_vm_t *vm,
    tinypy_diagnostic_callback_t callback,
    void *user_data) {
    tinypy_cycle_diagnostics_state_t *state = vm->cycle_diagnostics;
    tinypy_cycle_diagnostics_value_t *record;

    if (state == NULL) {
        return 0U;
    }
    for (record = state->values; record != NULL; record = record->next) {
        record->reachable = TINYPY_FALSE;
    }
    tinypy_internal_vm_visit_reachable_values(
        vm,
        __tinypy_cycle_diagnostics_mark_reachable,
        vm);
    size_t return_value_1 = __tinypy_debug_report_unreachable_cycles(
        vm,
        callback,
        user_data);
    return return_value_1;
}
#endif
//////////////////////////////////////////////////////////////////////////
size_t tinypy_vm_report_cycles(
    tinypy_vm_t *vm,
    tinypy_diagnostic_callback_t callback,
    void *user_data) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    if (vm->cycle_diagnostics == NULL || callback == NULL) {
        return 0U;
    }
    size_t return_value_1 = __tinypy_debug_report_cycles(vm, callback, user_data);
    return return_value_1;
#else
    (void)vm;
    (void)callback;
    (void)user_data;
    return 0U;
#endif
}
