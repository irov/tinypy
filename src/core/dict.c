#include "tinypy/dict.h"

#include "internal.h"

#include <string.h>

#define TINYPY_DICT_INITIAL_CAPACITY ((size_t)TINYPY_DICT_MIN_SIZE)
#define TINYPY_DICT_PERTURB_SHIFT 5U
#define TINYPY_DICT_CAPACITY(value) (TINYPY_DICT_OBJECT(value)->mask + 1U)
typedef struct tinypy_dict_lookup_t {
    size_t index;
    tinypy_bool_t found;
} tinypy_dict_lookup_t;
static inline size_t __tinypy_internal_dict_table_size(size_t capacity) {
    return capacity * sizeof(tinypy_dict_entry_t);
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_dict_probe_next(size_t index, uint64_t perturb, size_t mask) {
    return (index * 5U + 1U + (size_t)perturb) & mask;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_hash_t __tinypy_internal_dict_hash_key(const tinypy_vm_t *vm, const tinypy_value_t *key) {
    if (key->type == &vm->types[TINYPY_VALUE_INTEGER] || key->type == &vm->types[TINYPY_VALUE_BOOL]) {
        int64_t value = TINYPY_INTEGER_VALUE(key);

        return value == INT64_C(-1) ? (tinypy_hash_t)-2 : (tinypy_hash_t)value;
    }
    if (key->type == &vm->types[TINYPY_VALUE_STRING] && TINYPY_STRING_OBJECT(key)->hash_computed != 0) {
        tinypy_hash_t return_value_1 = TINYPY_STRING_OBJECT(key)->hash;
        return return_value_1;
    }
    if (key->type == &vm->types[TINYPY_VALUE_UNICODE] && TINYPY_UNICODE_OBJECT(key)->hash_computed != 0) {
        tinypy_hash_t return_value_2 = TINYPY_UNICODE_OBJECT(key)->hash;
        return return_value_2;
    }
    tinypy_hash_t return_value_3 = tinypy_internal_hash_value(key, NULL);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_internal_dict_keys_equal(const tinypy_vm_t *vm, const tinypy_value_t *left, const tinypy_value_t *right) {
    if (left == right) {
        return TINYPY_TRUE;
    }
    if (left->type == right->type) {
        if (left->type == &vm->types[TINYPY_VALUE_INTEGER] || left->type == &vm->types[TINYPY_VALUE_BOOL]) {
            tinypy_bool_t return_value_1 = TINYPY_INTEGER_VALUE(left) == TINYPY_INTEGER_VALUE(right) ? TINYPY_TRUE : TINYPY_FALSE;
            return return_value_1;
        }
        if (left->type == &vm->types[TINYPY_VALUE_STRING]) {
            size_t size = TINYPY_STRING_SIZE(left);

            tinypy_bool_t return_value_2 = size == TINYPY_STRING_SIZE(right) && (size == 0U || memcmp(TINYPY_STRING_OBJECT(left)->bytes, TINYPY_STRING_OBJECT(right)->bytes, size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
            return return_value_2;
        }
        if (left->type == &vm->types[TINYPY_VALUE_UNICODE]) {
            size_t size = TINYPY_UNICODE_OBJECT(left)->byte_size;

            tinypy_bool_t return_value_3 = size == TINYPY_UNICODE_OBJECT(right)->byte_size && (size == 0U || memcmp(TINYPY_UNICODE_OBJECT(left)->utf8, TINYPY_UNICODE_OBJECT(right)->utf8, size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
            return return_value_3;
        }
    }
    int32_t comparison = tinypy_compare_bool(
        (tinypy_value_t *)left,
        (tinypy_value_t *)right,
        TINYPY_COMPARE_EQUAL,
        NULL);
    return comparison > 0 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_lookup(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_hash_t hash, tinypy_dict_lookup_t *out_lookup) {
    const tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(dict)->table;
    size_t capacity = TINYPY_DICT_CAPACITY(dict);
    size_t first_dummy = SIZE_MAX;
    size_t mask;
    size_t index;
    uint64_t perturb;

    out_lookup->index = 0U;
    out_lookup->found = 0;
    mask = capacity - 1U;
    index = (size_t)((uint64_t)hash & (uint64_t)mask);
    perturb = (uint64_t)hash;
    for (;;) {
        const tinypy_dict_entry_t *entry = &entries[index];

        if (entry->state == TINYPY_DICT_ENTRY_EMPTY) {
            out_lookup->index = first_dummy != SIZE_MAX ? first_dummy : index;
            return;
        }
        if (entry->state == TINYPY_DICT_ENTRY_DUMMY) {
            if (first_dummy == SIZE_MAX) {
                first_dummy = index;
            }
        }
        else if (entry->hash == hash) {
            if (__tinypy_internal_dict_keys_equal(vm, entry->key, key) != 0) {
                out_lookup->index = index;
                out_lookup->found = 1;
                return;
            }
        }
        index = __tinypy_internal_dict_probe_next(index, perturb, mask);
        perturb >>= TINYPY_DICT_PERTURB_SHIFT;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    if (left == right) {
        return TINYPY_TRUE;
    }
    if (TINYPY_DICT_OBJECT(left)->used !=
        TINYPY_DICT_OBJECT(right)->used) {
        return TINYPY_FALSE;
    }
    const tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(left);
    const tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(left);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_dict_lookup_t lookup;

        if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        __tinypy_internal_dict_lookup(
            vm, right, iterator->key, iterator->hash, &lookup);
        if (lookup.found == 0) {
            return TINYPY_FALSE;
        }
        if (tinypy_internal_equal_value(
                iterator->value,
                TINYPY_DICT_OBJECT(right)->table[lookup.index].value,
                1) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_insert_clean(tinypy_dict_entry_t *entries, size_t capacity, tinypy_hash_t hash, tinypy_value_t *key, tinypy_value_t *value) {
    size_t mask = capacity - 1U;
    size_t index = (size_t)((uint64_t)hash & (uint64_t)mask);
    uint64_t perturb = (uint64_t)hash;

    while (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
        index = __tinypy_internal_dict_probe_next(index, perturb, mask);
        perturb >>= TINYPY_DICT_PERTURB_SHIFT;
    }
    entries[index].hash = hash;
    entries[index].key = key;
    entries[index].value = value;
    entries[index].state = TINYPY_DICT_ENTRY_ACTIVE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_resize(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_capacity) {
    tinypy_dict_entry_t *old_entries = TINYPY_DICT_OBJECT(dict)->table;
    size_t old_capacity = TINYPY_DICT_CAPACITY(dict);
    size_t new_capacity = TINYPY_DICT_INITIAL_CAPACITY;
    size_t new_size;
    size_t old_size;
    tinypy_dict_entry_t *iterator_end;

    while (new_capacity < minimum_capacity) {
        new_capacity *= 2U;
    }
    new_size = __tinypy_internal_dict_table_size(new_capacity);

    tinypy_dict_entry_t *new_entries = (tinypy_dict_entry_t *)tinypy_internal_vm_allocate(
        vm, new_size);
    (void)memset(new_entries, 0, new_size);

    tinypy_dict_entry_t *iterator = old_entries;
    iterator_end = old_entries + old_capacity;
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            __tinypy_internal_dict_insert_clean(
                new_entries,
                new_capacity,
                iterator->hash,
                iterator->key,
                iterator->value);
        }
    }
    if (old_entries != TINYPY_DICT_OBJECT(dict)->small_table) {
        old_size = __tinypy_internal_dict_table_size(old_capacity);
        tinypy_internal_vm_deallocate(
            vm,
            old_entries,
            old_size);
    }
    TINYPY_DICT_OBJECT(dict)->table = new_entries;
    TINYPY_DICT_OBJECT(dict)->mask = new_capacity - 1U;
    TINYPY_DICT_OBJECT(dict)->fill = TINYPY_DICT_OBJECT(dict)->used;
    return;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_needs_resize(const tinypy_value_t *dict) {
    size_t capacity = TINYPY_DICT_CAPACITY(dict);
    size_t fill = TINYPY_DICT_OBJECT(dict)->fill;

    return fill >= capacity - capacity / 3U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(value);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(value);

    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            visit(iterator->key, user_data);
            visit(iterator->value, user_data);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_destroy(tinypy_value_t *value) {
    tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(value)->table;

    if (entries != TINYPY_DICT_OBJECT(value)->small_table) {
        size_t table_size = __tinypy_internal_dict_table_size(TINYPY_DICT_CAPACITY(value));

        tinypy_internal_vm_deallocate(
            TINYPY_VALUE_VM(value),
            entries,
            table_size);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_new(tinypy_vm_t *vm) {
    tinypy_value_t *dict = tinypy_internal_value_allocate(
        vm, TINYPY_VALUE_DICT, sizeof(tinypy_dict_object_t));
    TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
    TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
    return dict;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_dict_size(const tinypy_value_t *dict) {

    size_t return_value_1 = TINYPY_DICT_OBJECT(dict)->used;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_find_public(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_dict_lookup_t *out_lookup, tinypy_hash_t *out_hash) {
    *out_hash = __tinypy_internal_dict_hash_key(vm, key);
    __tinypy_internal_dict_lookup(vm, dict, key, *out_hash, out_lookup);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_get(const tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    const tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    __tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash);
    tinypy_value_t *return_value_1 = TINYPY_DICT_OBJECT(dict)->table[lookup.index].value;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_get_optional(const tinypy_value_t *dict, const tinypy_value_t *key) {
    const tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_value_t *return_value_1 = tinypy_internal_dict_get_optional(vm, dict, key);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_get_optional(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_value_t *return_value_1 = tinypy_internal_dict_get_optional_index(vm, dict, key, NULL, NULL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_get_optional_index(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t *out_index, tinypy_value_t **out_stored_key) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    __tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash);
    if (out_index != NULL) {
        *out_index = lookup.index;
    }
    if (out_stored_key != NULL) {
        *out_stored_key = lookup.found != 0 ? TINYPY_DICT_OBJECT(dict)->table[lookup.index].key : NULL;
    }
    tinypy_value_t *return_value_1 = lookup.found != 0 ? TINYPY_DICT_OBJECT(dict)->table[lookup.index].value : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_get_index_hint(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t index) {
    const tinypy_dict_object_t *dict_object = TINYPY_DICT_OBJECT((tinypy_value_t *)dict);

    if (index > dict_object->mask) {
        return NULL;
    }
    const tinypy_dict_entry_t *entry = &dict_object->table[index];
    if (entry->state != TINYPY_DICT_ENTRY_ACTIVE || __tinypy_internal_dict_keys_equal(vm, entry->key, key) == 0) {
        return NULL;
    }
    return entry->value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_dict_contains(const tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    const tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    __tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash);
    return (int32_t)lookup.found;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    __tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash);
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_lookup_cache_invalidate(vm);
    }

    if (lookup.found != 0) {
        TINYPY_INCREF(value);
        tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
        tinypy_value_t *previous = entry->value;
        entry->value = value;
        TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_dict_set(vm, dict, entry->key, value, 0);
#endif
        TINYPY_DECREF(previous);
        return;
    }

    if (__tinypy_internal_dict_needs_resize(dict)) {
        size_t minimum = TINYPY_DICT_CAPACITY(dict) * 2U;
        __tinypy_internal_dict_resize(vm, dict, minimum);
        __tinypy_internal_dict_lookup(vm, dict, key, hash, &lookup);
    }

    TINYPY_INCREF(key);
    TINYPY_INCREF(value);
    tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
    if (entry->state == TINYPY_DICT_ENTRY_EMPTY) {
        TINYPY_DICT_OBJECT(dict)->fill += 1U;
    }
    entry->hash = hash;
    entry->key = key;
    entry->value = value;
    entry->state = TINYPY_DICT_ENTRY_ACTIVE;
    TINYPY_DICT_OBJECT(dict)->used += 1U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_set(vm, dict, entry->key, value, 1);
#endif
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_delete_optional(tinypy_vm_t *vm, tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_dict_lookup_t lookup;
    tinypy_value_t *owned_value;
    tinypy_hash_t hash;

    __tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash);
    if (lookup.found == 0) {
        return TINYPY_FALSE;
    }
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_lookup_cache_invalidate(vm);
    }

    tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
    tinypy_value_t *owned_key = entry->key;
    owned_value = entry->value;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_delete(vm, dict, entry->key);
#endif
    entry->key = NULL;
    entry->value = NULL;
    entry->state = TINYPY_DICT_ENTRY_DUMMY;
    TINYPY_DICT_OBJECT(dict)->used -= 1U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
    TINYPY_DECREF(owned_key);
    TINYPY_DECREF(owned_value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_dict_delete(tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_bool_t deleted;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    deleted = tinypy_internal_dict_delete_optional(vm, dict, key);
    (void)deleted;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_dict_clear(tinypy_value_t *dict) {
    tinypy_dict_entry_t *iterator_end;
    size_t capacity;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    if (TINYPY_DICT_OBJECT(dict)->used == 0U) {
        return;
    }
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_lookup_cache_invalidate(vm);
    }

    tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(dict)->table;
    capacity = TINYPY_DICT_CAPACITY(dict);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_clear(vm, dict);
#endif
    TINYPY_DICT_OBJECT(dict)->used = 0U;
    TINYPY_DICT_OBJECT(dict)->fill = 0U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    iterator_end = TINYPY_DICT_ITERATOR_END(dict);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *key = iterator->key;
            tinypy_value_t *value = iterator->value;
            iterator->key = NULL;
            iterator->value = NULL;
            iterator->state = TINYPY_DICT_ENTRY_EMPTY;
            TINYPY_DECREF(key);
            TINYPY_DECREF(value);
        }
        else {
            iterator->state = TINYPY_DICT_ENTRY_EMPTY;
        }
    }
    if (entries != TINYPY_DICT_OBJECT(dict)->small_table) {
        size_t table_size;

        table_size = __tinypy_internal_dict_table_size(capacity);
        tinypy_internal_vm_deallocate(
            vm,
            entries,
            table_size);
        (void)memset(
            TINYPY_DICT_OBJECT(dict)->small_table,
            0,
            sizeof(TINYPY_DICT_OBJECT(dict)->small_table));
        TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
        TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
uint64_t tinypy_dict_version(const tinypy_value_t *dict) {

    uint64_t return_value_1 = TINYPY_DICT_OBJECT(dict)->mutation_version;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_dict_next(const tinypy_value_t *dict, size_t *position, tinypy_value_t **out_key, tinypy_value_t **out_value) {
    const tinypy_dict_object_t *object = TINYPY_DICT_OBJECT((tinypy_value_t *)dict);
    while (*position <= object->mask) {
        const tinypy_dict_entry_t *entry = &object->table[*position];

        *position += 1U;
        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            *out_key = entry->key;
            *out_value = entry->value;
            return TINYPY_TRUE;
        }
    }
    *out_key = NULL;
    *out_value = NULL;
    return TINYPY_FALSE;
}
