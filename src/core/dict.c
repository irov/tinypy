#include "tinypy/dict.h"

#include "internal.h"

#include <string.h>

#define TINYPY_DICT_INITIAL_CAPACITY ((size_t)TINYPY_DICT_MIN_SIZE)
#define TINYPY_DICT_PERTURB_SHIFT 5U
#define TINYPY_DICT_CAPACITY(value) (TINYPY_DICT_OBJECT(value)->mask + 1U)

typedef struct tinypy_dict_lookup_t {
    size_t index;
    int found;
} tinypy_dict_lookup_t;

static void __tinypy_internal_dict_validate(const tinypy_value_t *dict)
{
    assert(dict != NULL);
    assert(tinypy_internal_value_kind(dict) == TINYPY_VALUE_DICT);
    (void)dict;
}

static size_t __tinypy_internal_dict_table_size(size_t capacity)
{
    assert(capacity <= SIZE_MAX / sizeof(tinypy_dict_entry_t));
    return capacity * sizeof(tinypy_dict_entry_t);
}

static size_t __tinypy_internal_dict_probe_next(
    size_t index,
    uint64_t perturb,
    size_t mask)
{
    return (index * 5U + 1U + (size_t)perturb) & mask;
}

static void __tinypy_internal_dict_lookup(
    const tinypy_value_t *dict,
    const tinypy_value_t *key,
    tinypy_hash_t hash,
    tinypy_dict_lookup_t *out_lookup)
{
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
        } else if (entry->hash == hash) {
            if (tinypy_internal_equal_value(entry->key, key, 1) != 0) {
                out_lookup->index = index;
                out_lookup->found = 1;
                return;
            }
        }
        index = __tinypy_internal_dict_probe_next(index, perturb, mask);
        perturb >>= TINYPY_DICT_PERTURB_SHIFT;
    }
}

int32_t tinypy_internal_dict_equal(
    const tinypy_value_t *left,
    const tinypy_value_t *right)
{
    const tinypy_dict_entry_t *left_entries =
        TINYPY_DICT_OBJECT(left)->table;
    size_t left_capacity = TINYPY_DICT_CAPACITY(left);
    size_t index;
#ifndef NDEBUG
    tinypy_vm_t *vm;
#endif

    if (left == right) {
        return 1;
    }
    if (TINYPY_DICT_OBJECT(left)->used !=
        TINYPY_DICT_OBJECT(right)->used) {
        return 0;
    }
#ifndef NDEBUG
    vm = tinypy_internal_value_vm(left);
    assert(vm->equality_depth < 1000U);
    vm->equality_depth += 1U;
#endif
    for (index = 0U; index < left_capacity; index += 1U) {
        const tinypy_dict_entry_t *entry = &left_entries[index];
        tinypy_dict_lookup_t lookup;

        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        __tinypy_internal_dict_lookup(
            right, entry->key, entry->hash, &lookup);
        if (lookup.found == 0) {
#ifndef NDEBUG
            vm->equality_depth -= 1U;
#endif
            return 0;
        }
        if (tinypy_internal_equal_value(
                entry->value,
                TINYPY_DICT_OBJECT(right)->table[lookup.index].value,
                1) == 0) {
#ifndef NDEBUG
            vm->equality_depth -= 1U;
#endif
            return 0;
        }
    }
#ifndef NDEBUG
    vm->equality_depth -= 1U;
#endif
    return 1;
}

static void __tinypy_internal_dict_insert_clean(
    tinypy_dict_entry_t *entries,
    size_t capacity,
    tinypy_hash_t hash,
    tinypy_value_t *key,
    tinypy_value_t *value)
{
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

static void __tinypy_internal_dict_resize(
    tinypy_vm_t *vm,
    tinypy_value_t *dict,
    size_t minimum_capacity)
{
    tinypy_dict_entry_t *old_entries = TINYPY_DICT_OBJECT(dict)->table;
    size_t old_capacity = TINYPY_DICT_CAPACITY(dict);
    tinypy_dict_entry_t *new_entries;
    size_t new_capacity = TINYPY_DICT_INITIAL_CAPACITY;
    size_t new_size;
    size_t old_size;
    size_t index;

    while (new_capacity < minimum_capacity) {
        assert(new_capacity <= SIZE_MAX / 2U);
        new_capacity *= 2U;
    }
    new_size = __tinypy_internal_dict_table_size(new_capacity);

    new_entries = (tinypy_dict_entry_t *)tinypy_internal_vm_allocate(
        vm, new_size, (uint32_t)TINYPY_ALLOC_TAG_DICT_TABLE);
    (void)memset(new_entries, 0, new_size);

    for (index = 0U; index < old_capacity; index += 1U) {
        tinypy_dict_entry_t *entry = &old_entries[index];
        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            __tinypy_internal_dict_insert_clean(
                new_entries,
                new_capacity,
                entry->hash,
                entry->key,
                entry->value);
        }
    }
    if (old_entries != TINYPY_DICT_OBJECT(dict)->small_table) {
        old_size = __tinypy_internal_dict_table_size(old_capacity);
        tinypy_internal_vm_deallocate(
            vm,
            old_entries,
            old_size,
            (uint32_t)TINYPY_ALLOC_TAG_DICT_TABLE);
    }
    TINYPY_DICT_OBJECT(dict)->table = new_entries;
    TINYPY_DICT_OBJECT(dict)->mask = new_capacity - 1U;
    TINYPY_DICT_OBJECT(dict)->fill = TINYPY_DICT_OBJECT(dict)->used;
    return;
}

static int __tinypy_internal_dict_needs_resize(const tinypy_value_t *dict)
{
    size_t capacity = TINYPY_DICT_CAPACITY(dict);
    size_t fill = TINYPY_DICT_OBJECT(dict)->fill;

    return fill >= capacity - capacity / 3U;
}

void tinypy_internal_dict_release_references(
    tinypy_value_t *value,
    tinypy_release_callback_t visit,
    void *user_data)
{
    tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(value)->table;
    size_t capacity = TINYPY_DICT_CAPACITY(value);
    size_t index;

    for (index = 0U; index < capacity; index += 1U) {
        if (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
            visit(entries[index].key, user_data);
            visit(entries[index].value, user_data);
        }
    }
}

void tinypy_internal_dict_destroy(tinypy_vm_t *vm, tinypy_value_t *value)
{
    tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(value)->table;
    size_t capacity = TINYPY_DICT_CAPACITY(value);
    size_t table_size;

    if (entries != TINYPY_DICT_OBJECT(value)->small_table) {
        table_size = __tinypy_internal_dict_table_size(capacity);
        tinypy_internal_vm_deallocate(
            vm,
            entries,
            table_size,
            (uint32_t)TINYPY_ALLOC_TAG_DICT_TABLE);
    }
    (void)memset(
        TINYPY_DICT_OBJECT(value)->small_table,
        0,
        sizeof(TINYPY_DICT_OBJECT(value)->small_table));
    TINYPY_DICT_OBJECT(value)->table = TINYPY_DICT_OBJECT(value)->small_table;
    TINYPY_DICT_OBJECT(value)->mask = TINYPY_DICT_MIN_SIZE - 1U;
    TINYPY_DICT_OBJECT(value)->used = 0U;
    TINYPY_DICT_OBJECT(value)->fill = 0U;
}

tinypy_value_t *tinypy_dict_new(tinypy_vm_t *vm)
{
    tinypy_value_t *dict;

    assert(tinypy_internal_vm_valid(vm));
    dict = tinypy_internal_value_allocate(
        vm, TINYPY_VALUE_DICT, sizeof(tinypy_dict_object_t));
    TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
    TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
    return dict;
}

size_t tinypy_dict_size(const tinypy_value_t *dict)
{
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(dict)));
    __tinypy_internal_dict_validate(dict);

    return TINYPY_DICT_OBJECT(dict)->used;
}

static void __tinypy_internal_dict_find_public(
    const tinypy_value_t *dict,
    const tinypy_value_t *key,
    tinypy_dict_lookup_t *out_lookup,
    tinypy_hash_t *out_hash)
{
    *out_hash = tinypy_internal_hash_value(key);
    __tinypy_internal_dict_lookup(dict, key, *out_hash, out_lookup);
}

tinypy_value_t *tinypy_dict_get(
    const tinypy_value_t *dict,
    const tinypy_value_t *key)
{
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(dict)));
    __tinypy_internal_dict_validate(dict);
    assert(tinypy_internal_value_belongs_to(
        tinypy_internal_value_vm(dict), key));
    __tinypy_internal_dict_find_public(dict, key, &lookup, &hash);
    assert(lookup.found != 0);
    return TINYPY_DICT_OBJECT(dict)->table[lookup.index].value;
}

int32_t tinypy_dict_contains(
    const tinypy_value_t *dict,
    const tinypy_value_t *key)
{
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(dict)));
    __tinypy_internal_dict_validate(dict);
    assert(tinypy_internal_value_belongs_to(
        tinypy_internal_value_vm(dict), key));
    __tinypy_internal_dict_find_public(dict, key, &lookup, &hash);
    return (int32_t)lookup.found;
}

void tinypy_dict_set(
    tinypy_value_t *dict,
    tinypy_value_t *key,
    tinypy_value_t *value)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(dict);
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;
    tinypy_dict_entry_t *entry;

    assert(tinypy_internal_vm_valid(vm));
    __tinypy_internal_dict_validate(dict);
    assert(tinypy_internal_value_belongs_to(vm, key));
    assert(tinypy_internal_value_belongs_to(vm, value));
    __tinypy_internal_dict_find_public(dict, key, &lookup, &hash);
    assert(TINYPY_DICT_OBJECT(dict)->mutation_version != UINT64_MAX);

    if (lookup.found != 0) {
        tinypy_value_t *previous;

        tinypy_retain(value);
        entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
        previous = entry->value;
        entry->value = value;
        TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
        tinypy_release(previous);
        return;
    }

    if (__tinypy_internal_dict_needs_resize(dict)) {
        size_t minimum = TINYPY_DICT_CAPACITY(dict) * 2U;
        assert(minimum >= TINYPY_DICT_CAPACITY(dict));
        __tinypy_internal_dict_resize(vm, dict, minimum);
        __tinypy_internal_dict_lookup(dict, key, hash, &lookup);
    }

    tinypy_retain(key);
    tinypy_retain(value);
    entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
    if (entry->state == TINYPY_DICT_ENTRY_EMPTY) {
        TINYPY_DICT_OBJECT(dict)->fill += 1U;
    }
    entry->hash = hash;
    entry->key = key;
    entry->value = value;
    entry->state = TINYPY_DICT_ENTRY_ACTIVE;
    TINYPY_DICT_OBJECT(dict)->used += 1U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
}

void tinypy_dict_delete(
    tinypy_value_t *dict,
    const tinypy_value_t *key)
{
    tinypy_dict_lookup_t lookup;
    tinypy_dict_entry_t *entry;
    tinypy_value_t *owned_key;
    tinypy_value_t *owned_value;
    tinypy_hash_t hash;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(dict)));
    __tinypy_internal_dict_validate(dict);
    assert(tinypy_internal_value_belongs_to(
        tinypy_internal_value_vm(dict), key));
    __tinypy_internal_dict_find_public(dict, key, &lookup, &hash);
    assert(lookup.found != 0);
    assert(TINYPY_DICT_OBJECT(dict)->mutation_version != UINT64_MAX);

    entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
    owned_key = entry->key;
    owned_value = entry->value;
    entry->key = NULL;
    entry->value = NULL;
    entry->state = TINYPY_DICT_ENTRY_DUMMY;
    TINYPY_DICT_OBJECT(dict)->used -= 1U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
    tinypy_release(owned_key);
    tinypy_release(owned_value);
}

void tinypy_dict_clear(tinypy_value_t *dict)
{
    tinypy_dict_entry_t *entries;
    size_t capacity;
    size_t index;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(dict)));
    __tinypy_internal_dict_validate(dict);
    if (TINYPY_DICT_OBJECT(dict)->used == 0U) {
        return;
    }
    assert(TINYPY_DICT_OBJECT(dict)->mutation_version != UINT64_MAX);

    entries = TINYPY_DICT_OBJECT(dict)->table;
    capacity = TINYPY_DICT_CAPACITY(dict);
    TINYPY_DICT_OBJECT(dict)->used = 0U;
    TINYPY_DICT_OBJECT(dict)->fill = 0U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
    for (index = 0U; index < capacity; index += 1U) {
        if (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *key = entries[index].key;
            tinypy_value_t *value = entries[index].value;
            entries[index].key = NULL;
            entries[index].value = NULL;
            entries[index].state = TINYPY_DICT_ENTRY_EMPTY;
            tinypy_release(key);
            tinypy_release(value);
        } else {
            entries[index].state = TINYPY_DICT_ENTRY_EMPTY;
        }
    }
    if (entries != TINYPY_DICT_OBJECT(dict)->small_table) {
        size_t table_size;

        table_size = __tinypy_internal_dict_table_size(capacity);
        tinypy_internal_vm_deallocate(
            tinypy_internal_value_vm(dict),
            entries,
            table_size,
            (uint32_t)TINYPY_ALLOC_TAG_DICT_TABLE);
        (void)memset(
            TINYPY_DICT_OBJECT(dict)->small_table,
            0,
            sizeof(TINYPY_DICT_OBJECT(dict)->small_table));
        TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
        TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
    }
}

uint64_t tinypy_dict_version(const tinypy_value_t *dict)
{
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(dict)));
    __tinypy_internal_dict_validate(dict);

    return TINYPY_DICT_OBJECT(dict)->mutation_version;
}
