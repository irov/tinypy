#include "tinypy/dict.h"

#include "internal.h"

#include <string.h>

#define TINYPY_DICT_PERTURB_SHIFT 5U
#define TINYPY_DICT_CAPACITY(value) (TINYPY_DICT_OBJECT(value)->mask + 1U)
typedef struct tinypy_dict_lookup_t {
    size_t index;
    tinypy_bool_t found;
    tinypy_bool_t cacheable;
} tinypy_dict_lookup_t;
/* Code caches borrow values; VM-wide generations prevent dictionary address reuse
   or content swaps from making an old borrowed value look current. */
static void __tinypy_internal_dict_modified(tinypy_value_t *dict) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);

    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
    vm->dict_cache_epoch += UINT64_C(1);
    if (vm->dict_cache_epoch == 0U) {
        vm->dict_cache_exhausted = TINYPY_TRUE;
    }
    TINYPY_DICT_OBJECT(dict)->cache_version = vm->dict_cache_epoch;
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_dict_table_size(size_t capacity) {
    return capacity * sizeof(tinypy_dict_entry_t);
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_dict_probe_next(size_t index, uint64_t perturb, size_t mask) {
    return (index * 5U + 1U + (size_t)perturb) & mask;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_internal_dict_hash_key(const tinypy_vm_t *vm, const tinypy_value_t *key, tinypy_hash_t *out_hash, tinypy_error_t **out_error) {
    if (key->type == &vm->types[TINYPY_VALUE_INTEGER] || key->type == &vm->types[TINYPY_VALUE_BOOL]) {
        int64_t value = TINYPY_INTEGER_VALUE(key);

        *out_hash = value == INT64_C(-1) ? (tinypy_hash_t)-2 : (tinypy_hash_t)value;
        return TINYPY_TRUE;
    }
    if (key->type == &vm->types[TINYPY_VALUE_STRING] && TINYPY_STRING_OBJECT(key)->hash_computed != 0) {
        *out_hash = TINYPY_STRING_OBJECT(key)->hash;
        return TINYPY_TRUE;
    }
    if (key->type == &vm->types[TINYPY_VALUE_UNICODE] && TINYPY_UNICODE_OBJECT(key)->hash_computed != 0) {
        *out_hash = TINYPY_UNICODE_OBJECT(key)->hash;
        return TINYPY_TRUE;
    }
    tinypy_value_t *previous_raised = ((tinypy_vm_t *)vm)->raised_value;

    *out_hash = tinypy_internal_hash_value(key, out_error);
    return (out_error != NULL && *out_error != NULL) || ((tinypy_vm_t *)vm)->raised_value != previous_raised ? TINYPY_FALSE : TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_hash_checked(const tinypy_vm_t *vm, const tinypy_value_t *key, tinypy_hash_t *out_hash, tinypy_error_t **out_error) {
    tinypy_bool_t hashed = __tinypy_internal_dict_hash_key(vm, key, out_hash, out_error);

    return hashed;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_internal_dict_keys_equal(const tinypy_vm_t *vm, const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    if (left == right) {
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (left->type == right->type) {
        if (left->type == &vm->types[TINYPY_VALUE_INTEGER] || left->type == &vm->types[TINYPY_VALUE_BOOL]) {
            *out_equal = TINYPY_INTEGER_VALUE(left) == TINYPY_INTEGER_VALUE(right) ? TINYPY_TRUE : TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        if (left->type == &vm->types[TINYPY_VALUE_STRING]) {
            size_t size = TINYPY_STRING_SIZE(left);

            *out_equal = size == TINYPY_STRING_SIZE(right) && (size == 0U || memcmp(TINYPY_STRING_OBJECT(left)->bytes, TINYPY_STRING_OBJECT(right)->bytes, size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        if (left->type == &vm->types[TINYPY_VALUE_UNICODE]) {
            size_t size = TINYPY_UNICODE_OBJECT(left)->byte_size;

            *out_equal = size == TINYPY_UNICODE_OBJECT(right)->byte_size && (size == 0U || memcmp(TINYPY_UNICODE_OBJECT(left)->utf8, TINYPY_UNICODE_OBJECT(right)->utf8, size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
            return TINYPY_TRUE;
        }
    }
    int32_t comparison = tinypy_compare_bool(
        (tinypy_value_t *)left,
        (tinypy_value_t *)right,
        TINYPY_COMPARE_EQUAL,
        out_error);
    if (comparison < 0) {
        return TINYPY_FALSE;
    }
    *out_equal = comparison > 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* lookdict: a key comparison that replaces the table or the compared entry
   restarts the probe. The free slot reported for a missing key may still have
   been filled by a comparison, so inserting callers examine it afterwards. */
static tinypy_bool_t __tinypy_internal_dict_lookup(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_hash_t hash, tinypy_dict_lookup_t *out_lookup, tinypy_error_t **out_error) {
    const tinypy_dict_entry_t *entries;
    size_t first_dummy = SIZE_MAX;
    size_t mask;
    size_t index;
    uint64_t perturb;

    out_lookup->cacheable = TINYPY_TRUE;
restart:
    out_lookup->index = 0U;
    out_lookup->found = 0;
    entries = TINYPY_DICT_OBJECT(dict)->table;
    first_dummy = SIZE_MAX;
    mask = TINYPY_DICT_OBJECT(dict)->mask;
    index = (size_t)((uint64_t)hash & (uint64_t)mask);
    perturb = (uint64_t)hash;
    for (;;) {
        const tinypy_dict_entry_t *entry = &entries[index];

        if (TINYPY_DICT_ENTRY_IS_EMPTY(entry)) {
            out_lookup->index = first_dummy != SIZE_MAX ? first_dummy : index;
            return TINYPY_TRUE;
        }
        if (TINYPY_DICT_ENTRY_IS_DUMMY(entry)) {
            if (first_dummy == SIZE_MAX) {
                first_dummy = index;
            }
        }
        else if (entry->hash == hash) {
            tinypy_value_t *stored_key = entry->key;
            tinypy_bool_t equal;

            if (stored_key == key) {
                out_lookup->index = index;
                out_lookup->found = 1;
                return TINYPY_TRUE;
            }
            if (stored_key->type == key->type
                && (stored_key->type == &vm->types[TINYPY_VALUE_INTEGER]
                    || stored_key->type == &vm->types[TINYPY_VALUE_BOOL]
                    || stored_key->type == &vm->types[TINYPY_VALUE_STRING]
                    || stored_key->type == &vm->types[TINYPY_VALUE_UNICODE])) {
                if (__tinypy_internal_dict_keys_equal(vm, stored_key, key, &equal, out_error) == 0) {
                    return TINYPY_FALSE;
                }
                if (equal != 0) {
                    out_lookup->index = index;
                    out_lookup->found = 1;
                    return TINYPY_TRUE;
                }
                index = __tinypy_internal_dict_probe_next(index, perturb, mask);
                perturb >>= TINYPY_DICT_PERTURB_SHIFT;
                continue;
            }
            out_lookup->cacheable = TINYPY_FALSE;
            TINYPY_INCREF(stored_key);
            tinypy_bool_t compared = __tinypy_internal_dict_keys_equal(vm, stored_key, key, &equal, out_error);
            TINYPY_DECREF(stored_key);
            if (compared == 0) {
                return TINYPY_FALSE;
            }
            if (TINYPY_DICT_OBJECT(dict)->table != entries || TINYPY_DICT_OBJECT(dict)->mask != mask || entry->key != stored_key) {
                goto restart;
            }
            if (equal != 0) {
                out_lookup->index = index;
                out_lookup->found = 1;
                return TINYPY_TRUE;
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

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            continue;
        }
        if (__tinypy_internal_dict_lookup(
                vm, right, iterator->key, iterator->hash, &lookup, NULL) == 0) {
            return TINYPY_FALSE;
        }
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
tinypy_bool_t tinypy_internal_dict_equal_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    size_t index;

    if (left == right) {
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (TINYPY_DICT_OBJECT(left)->used != TINYPY_DICT_OBJECT(right)->used) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    for (index = 0U; index < TINYPY_DICT_CAPACITY(left); ++index) {
        const tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(left)->table[index];
        tinypy_value_t *left_key;
        tinypy_value_t *left_value;
        tinypy_value_t *right_value;
        int32_t equal;

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            continue;
        }
        left_key = entry->key;
        left_value = entry->value;
        TINYPY_INCREF(left_key);
        TINYPY_INCREF(left_value);
        right_value = tinypy_internal_dict_get_optional_suppressed(vm, right, left_key);
        if (right_value == NULL) {
            TINYPY_DECREF(left_key);
            TINYPY_DECREF(left_value);
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        TINYPY_INCREF(right_value);
        equal = left_value == right_value ? 1 : tinypy_compare_bool(left_value, right_value, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(right_value);
        TINYPY_DECREF(left_key);
        TINYPY_DECREF(left_value);
        if (equal < 0) {
            return TINYPY_FALSE;
        }
        if (equal == 0) {
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
    }
    *out_equal = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_insert_clean(tinypy_dict_entry_t *entries, size_t capacity, tinypy_hash_t hash, tinypy_value_t *key, tinypy_value_t *value) {
    size_t mask = capacity - 1U;
    size_t index = (size_t)((uint64_t)hash & (uint64_t)mask);
    uint64_t perturb = (uint64_t)hash;

    while (TINYPY_DICT_ENTRY_IS_ACTIVE(&entries[index])) {
        index = __tinypy_internal_dict_probe_next(index, perturb, mask);
        perturb >>= TINYPY_DICT_PERTURB_SHIFT;
    }
    entries[index].hash = hash;
    entries[index].key = key;
    entries[index].value = value;
}
//////////////////////////////////////////////////////////////////////////
/* dictresize sizes a table as the smallest power of two above minimum_used. */
static tinypy_bool_t __tinypy_internal_dict_capacity(tinypy_vm_t *vm, size_t minimum_used, size_t *out_capacity, tinypy_error_t **out_error) {
    size_t capacity = TINYPY_DICT_MIN_SIZE;

    while (capacity <= minimum_used) {
        if (capacity > SIZE_MAX / 2U / sizeof(tinypy_dict_entry_t)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "dictionary is too large", out_error);
            return TINYPY_FALSE;
        }
        capacity *= 2U;
    }
    *out_capacity = capacity;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The minimum size uses the embedded small table, so only larger checked
   tables can fail to allocate. */
static tinypy_dict_entry_t *__tinypy_internal_dict_table_allocate(tinypy_vm_t *vm, tinypy_value_t *dict, size_t capacity, tinypy_bool_t checked, tinypy_error_t **out_error) {
    tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(dict)->small_table;

    if (capacity != TINYPY_DICT_MIN_SIZE) {
        size_t table_size = __tinypy_internal_dict_table_size(capacity);

        entries = (tinypy_dict_entry_t *)(checked != 0
                                              ? tinypy_internal_vm_allocate_checked(vm, table_size, out_error)
                                              : tinypy_internal_vm_allocate(vm, table_size));
    }
    return entries;
}
//////////////////////////////////////////////////////////////////////////
/* dictresize moves the live entries in table order into the new table, which
   then holds no deleted slots. */
static void __tinypy_internal_dict_rebuild(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_dict_entry_t *entries, size_t capacity) {
    tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(dict);
    tinypy_dict_entry_t small_copy[TINYPY_DICT_MIN_SIZE];
    tinypy_dict_entry_t *old_entries = object->table;
    size_t old_capacity = object->mask + 1U;
    tinypy_bool_t old_allocated = old_entries != object->small_table ? TINYPY_TRUE : TINYPY_FALSE;
    size_t index;

    if (old_entries == entries) {
        (void)memcpy(small_copy, old_entries, sizeof(small_copy));
        old_entries = small_copy;
    }
    (void)memset(entries, 0, __tinypy_internal_dict_table_size(capacity));
    for (index = 0U; index < old_capacity; ++index) {
        const tinypy_dict_entry_t *entry = &old_entries[index];

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            __tinypy_internal_dict_insert_clean(entries, capacity, entry->hash, entry->key, entry->value);
        }
    }
    if (old_allocated != 0) {
        tinypy_internal_vm_deallocate(vm, old_entries, __tinypy_internal_dict_table_size(old_capacity));
    }
    object->table = entries;
    object->mask = capacity - 1U;
    object->fill = object->used;
}
//////////////////////////////////////////////////////////////////////////
/* dictresize; a small table without deleted slots is already as compact as
   it gets. */
tinypy_bool_t tinypy_internal_dict_resize_checked(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_used, tinypy_error_t **out_error) {
    tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(dict);
    size_t capacity;

    if (__tinypy_internal_dict_capacity(vm, minimum_used, &capacity, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (capacity == TINYPY_DICT_MIN_SIZE && object->table == object->small_table && object->fill == object->used) {
        return TINYPY_TRUE;
    }
    tinypy_dict_entry_t *entries = __tinypy_internal_dict_table_allocate(vm, dict, capacity, TINYPY_TRUE, out_error);
    if (entries == NULL) {
        return TINYPY_FALSE;
    }
    __tinypy_internal_dict_rebuild(vm, dict, entries, capacity);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* PyDict_Merge and set_merge resize once before adding the entries of another
   table, expecting few of them to be present already. */
tinypy_bool_t tinypy_internal_dict_merge_reserve_checked(tinypy_vm_t *vm, tinypy_value_t *dict, size_t incoming, tinypy_error_t **out_error) {
    tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(dict);

    if ((object->fill + incoming) * 3U < (object->mask + 1U) * 2U) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t resized = tinypy_internal_dict_resize_checked(vm, dict, (object->used + incoming) * 2U, out_error);
    return resized;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(value);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(value);

    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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
void tinypy_internal_dict_initialize_empty(tinypy_value_t *dict) {
    __tinypy_internal_dict_modified(dict);
    TINYPY_DICT_OBJECT(dict)->mutation_version = UINT64_C(0);
    TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
    TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
}
//////////////////////////////////////////////////////////////////////////
/* PyDict_Copy: the copy is presized like PyDict_Merge and receives the
   entries in table order with their stored hashes. */
tinypy_value_t *tinypy_internal_dict_copy(tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(source);
    tinypy_value_t *result = tinypy_dict_new(vm);
    size_t used = TINYPY_DICT_SIZE(source);

    if (tinypy_internal_dict_merge_reserve_checked(vm, result, used, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    tinypy_dict_entry_t *entries = TINYPY_DICT_OBJECT(result)->table;
    size_t capacity = TINYPY_DICT_CAPACITY(result);
    for (size_t index = 0U; index <= TINYPY_DICT_OBJECT(source)->mask; ++index) {
        tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(source)->table[index];
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            TINYPY_INCREF(entry->key);
            TINYPY_INCREF(entry->value);
            __tinypy_internal_dict_insert_clean(entries, capacity, entry->hash, entry->key, entry->value);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
            __tinypy_internal_cycle_diagnostics_dict_set(vm, result, entry->key, entry->value, 1);
#endif
        }
    }
    TINYPY_DICT_OBJECT(result)->used = used;
    TINYPY_DICT_OBJECT(result)->fill = used;
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A copy with the table layout of the source, so it iterates in the same
   order; callers take it where Python 2.7 would use the source itself. */
tinypy_value_t *tinypy_internal_dict_snapshot(tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(source);
    const tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(source);
    size_t capacity = object->mask + 1U;
    tinypy_value_t *result = tinypy_internal_dict_new_checked(vm, out_error);

    if (result == NULL) {
        return NULL;
    }
    tinypy_dict_entry_t *entries = __tinypy_internal_dict_table_allocate(vm, result, capacity, TINYPY_TRUE, out_error);
    if (entries == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    (void)memcpy(entries, object->table, __tinypy_internal_dict_table_size(capacity));
    for (size_t index = 0U; index < capacity; ++index) {
        const tinypy_dict_entry_t *entry = &entries[index];

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            TINYPY_INCREF(entry->key);
            TINYPY_INCREF(entry->value);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
            __tinypy_internal_cycle_diagnostics_dict_set(vm, result, entry->key, entry->value, 1);
#endif
        }
    }
    TINYPY_DICT_OBJECT(result)->table = entries;
    TINYPY_DICT_OBJECT(result)->mask = object->mask;
    TINYPY_DICT_OBJECT(result)->fill = object->fill;
    TINYPY_DICT_OBJECT(result)->used = object->used;
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_swap_contents(tinypy_value_t *left, tinypy_value_t *right) {
    tinypy_dict_object_t *left_dict = TINYPY_DICT_OBJECT(left);
    tinypy_dict_object_t *right_dict = TINYPY_DICT_OBJECT(right);
    tinypy_dict_entry_t left_small_table[TINYPY_DICT_MIN_SIZE];
    tinypy_dict_entry_t right_small_table[TINYPY_DICT_MIN_SIZE];
    tinypy_bool_t left_small = left_dict->table == left_dict->small_table;
    tinypy_bool_t right_small = right_dict->table == right_dict->small_table;
    size_t fill = left_dict->fill;
    size_t used = left_dict->used;
    size_t mask = left_dict->mask;
    tinypy_dict_entry_t *table = left_dict->table;

    if (left_small != 0) {
        (void)memcpy(left_small_table, left_dict->small_table, sizeof(left_small_table));
    }
    if (right_small != 0) {
        (void)memcpy(right_small_table, right_dict->small_table, sizeof(right_small_table));
    }
    left_dict->fill = right_dict->fill;
    left_dict->used = right_dict->used;
    left_dict->mask = right_dict->mask;
    if (right_small != 0) {
        (void)memcpy(left_dict->small_table, right_small_table, sizeof(right_small_table));
        left_dict->table = left_dict->small_table;
    }
    else {
        left_dict->table = right_dict->table;
    }
    right_dict->fill = fill;
    right_dict->used = used;
    right_dict->mask = mask;
    if (left_small != 0) {
        (void)memcpy(right_dict->small_table, left_small_table, sizeof(left_small_table));
        right_dict->table = right_dict->small_table;
    }
    else {
        right_dict->table = table;
    }
    __tinypy_internal_dict_modified(left);
    __tinypy_internal_dict_modified(right);
    if (left_dict->type_dictionary != 0) {
        tinypy_internal_type_modified(left_dict->type_owner);
    }
    if (right_dict->type_dictionary != 0) {
        tinypy_internal_type_modified(right_dict->type_owner);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_new(tinypy_vm_t *vm) {
    tinypy_value_t *dict = tinypy_internal_value_allocate(
        vm, TINYPY_VALUE_DICT, sizeof(tinypy_dict_object_t));
    tinypy_internal_dict_initialize_empty(dict);
    return dict;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_new_checked(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_DICT], sizeof(tinypy_dict_object_t), out_error);

    if (result != NULL) {
        tinypy_internal_dict_initialize_empty(result);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_dict_size(const tinypy_value_t *dict) {

    size_t return_value_1 = TINYPY_DICT_OBJECT(dict)->used;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_find_public(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_dict_lookup_t *out_lookup, tinypy_hash_t *out_hash, tinypy_error_t **out_error) {
    tinypy_error_t *local_error = NULL;
    tinypy_error_t **error_target = out_error != NULL ? out_error : &local_error;
    tinypy_bool_t result;

    if (__tinypy_internal_dict_hash_key(vm, key, out_hash, error_target) == 0) {
        result = TINYPY_FALSE;
    }
    else {
        result = __tinypy_internal_dict_lookup(vm, dict, key, *out_hash, out_lookup, error_target);
    }
    if (local_error != NULL) {
        tinypy_error_release(local_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_get(const tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    const tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    if (__tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash, NULL) == 0) {
        return NULL;
    }
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
/* PyDict_GetItem consumes lookup failures and preserves an existing error.
   The returned value is borrowed, like the ordinary optional lookup. */
tinypy_value_t *tinypy_internal_dict_get_optional_suppressed(tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_value_t *result = tinypy_internal_dict_get_optional_suppressed_status(vm, dict, key, NULL);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_get_optional_suppressed_status(tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_bool_t *out_succeeded) {
    tinypy_internal_exception_state_t state;
    tinypy_error_t *lookup_error = NULL;
    tinypy_value_t *value = NULL;

    tinypy_internal_exception_preserve_begin(vm, &state);
    tinypy_bool_t succeeded = tinypy_internal_dict_get_optional_checked(vm, dict, key, &value, &lookup_error);
    if (lookup_error != NULL) {
        tinypy_error_release(lookup_error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    if (out_succeeded != NULL) {
        *out_succeeded = succeeded;
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_get_optional_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_dict_get_optional_index_checked(vm, dict, key, NULL, out_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* Evaluator names are exact strings. Report whether a lookup invoked the
   generic comparison path so LOAD_GLOBAL never caches away user callbacks. */
tinypy_bool_t tinypy_internal_dict_get_global(tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_value_t **out_value, tinypy_bool_t *out_cacheable, tinypy_error_t **out_error) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    if (__tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash, out_error) == 0) {
        *out_value = NULL;
        *out_cacheable = TINYPY_FALSE;
        return TINYPY_FALSE;
    }
    *out_value = lookup.found != 0 ? TINYPY_DICT_OBJECT(dict)->table[lookup.index].value : NULL;
    *out_cacheable = lookup.cacheable;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_get_optional_index_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t *out_index, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    if (__tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash, out_error) == 0) {
        *out_value = NULL;
        return TINYPY_FALSE;
    }
    if (out_index != NULL) {
        *out_index = lookup.index;
    }
    *out_value = lookup.found != 0 ? TINYPY_DICT_OBJECT(dict)->table[lookup.index].value : NULL;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_lookup_hash_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_hash_t hash, size_t *out_index, tinypy_bool_t *out_found, tinypy_error_t **out_error) {
    tinypy_dict_lookup_t lookup;

    if (__tinypy_internal_dict_lookup(vm, dict, key, hash, &lookup, out_error) == 0) {
        *out_found = TINYPY_FALSE;
        return TINYPY_FALSE;
    }
    if (out_index != NULL) {
        *out_index = lookup.index;
    }
    *out_found = lookup.found;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_get_optional_index(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t *out_index, tinypy_value_t **out_stored_key) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    if (__tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash, NULL) == 0) {
        return NULL;
    }
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
    tinypy_bool_t equal;
    /* The hint is only a fast path when comparison cannot call Python. */
    if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) == 0
        || (entry->key != key && (entry->key->type != key->type
            || (key->type != &vm->types[TINYPY_VALUE_STRING] && key->type != &vm->types[TINYPY_VALUE_UNICODE])))
        || __tinypy_internal_dict_keys_equal(vm, entry->key, key, &equal, NULL) == 0 || equal == 0) {
        return NULL;
    }
    return entry->value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_dict_contains(const tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_bool_t contains;

    const tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    if (tinypy_internal_dict_contains_checked(vm, dict, key, &contains, NULL) == 0) {
        return TINYPY_FALSE;
    }
    return contains;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_contains_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_bool_t *out_contains, tinypy_error_t **out_error) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    if (__tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash, out_error) == 0) {
        *out_contains = TINYPY_FALSE;
        return TINYPY_FALSE;
    }
    *out_contains = lookup.found;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_set_checked(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_hash_t hash;
    tinypy_bool_t return_value_1;

    if (__tinypy_internal_dict_hash_key(vm, key, &hash, out_error) == 0) {
        return TINYPY_FALSE;
    }
    return_value_1 = tinypy_internal_dict_set_hash_checked(vm, dict, key, value, hash, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* The common store into a key found without the table changing. */
static inline void __tinypy_internal_dict_replace_value(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_dict_entry_t *entry, tinypy_value_t *value) {
    tinypy_value_t *previous = entry->value;

    TINYPY_INCREF(value);
    entry->value = value;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_set(vm, dict, entry->key, value, TINYPY_FALSE);
#else
    (void)vm;
#endif
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_modified(TINYPY_DICT_OBJECT(dict)->type_owner);
    }
    __tinypy_internal_dict_modified(dict);
    TINYPY_DECREF(previous);
}
//////////////////////////////////////////////////////////////////////////
/* insertdict_by_entry, followed by the resize of PyDict_SetItem when grow is
   set and the dictionary gained keys since used was read. The entry is
   examined as it is now: a key comparison of the lookup may have filled the
   free slot, and a live entry there only takes the new value. A growing table
   is allocated before anything is stored, so running out of memory leaves the
   dictionary as it was; an unchecked table ignores the heap budget. */
static tinypy_bool_t __tinypy_internal_dict_store(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_hash_t hash, size_t index, size_t used, tinypy_bool_t grow, tinypy_bool_t checked, tinypy_error_t **out_error) {
    tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(dict);
    tinypy_dict_entry_t *entry = &object->table[index];
    tinypy_bool_t added = TINYPY_DICT_ENTRY_IS_ACTIVE(entry) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    size_t used_after = added != 0 ? object->used + 1U : object->used;
    size_t fill_after = TINYPY_DICT_ENTRY_IS_EMPTY(entry) ? object->fill + 1U : object->fill;
    tinypy_dict_entry_t *entries = NULL;
    size_t capacity = 0U;
    tinypy_value_t *previous = NULL;

    if (grow != 0 && used_after > used && fill_after * 3U >= (object->mask + 1U) * 2U) {
        size_t growth = used_after > 50000U ? 2U : 4U;

        if (__tinypy_internal_dict_capacity(vm, used_after * growth, &capacity, out_error) == 0) {
            return TINYPY_FALSE;
        }
        entries = __tinypy_internal_dict_table_allocate(vm, dict, capacity, checked, out_error);
        if (entries == NULL) {
            return TINYPY_FALSE;
        }
    }
    TINYPY_INCREF(value);
    if (added != 0) {
        TINYPY_INCREF(key);
        entry->hash = hash;
        entry->key = key;
        object->fill = fill_after;
        object->used = used_after;
    }
    else {
        previous = entry->value;
    }
    entry->value = value;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_set(vm, dict, entry->key, value, added);
#endif
    if (entries != NULL) {
        __tinypy_internal_dict_rebuild(vm, dict, entries, capacity);
    }
    if (object->type_dictionary != 0) {
        tinypy_internal_type_modified(object->type_owner);
    }
    __tinypy_internal_dict_modified(dict);
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_insert(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_hash_t hash, tinypy_bool_t grow, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t used = TINYPY_DICT_SIZE(dict);
    tinypy_dict_lookup_t lookup;

    if (__tinypy_internal_dict_lookup(vm, dict, key, hash, &lookup, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (lookup.found != 0 && TINYPY_DICT_SIZE(dict) == used) {
        __tinypy_internal_dict_replace_value(vm, dict, &TINYPY_DICT_OBJECT(dict)->table[lookup.index], value);
        return TINYPY_TRUE;
    }
    tinypy_bool_t stored = __tinypy_internal_dict_store(vm, dict, key, value, hash, lookup.index, used, grow, checked, out_error);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
/* The C API store is not a fallible language operation: its growth ignores
   the heap budget. */
void tinypy_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_hash_t hash;

    if (__tinypy_internal_dict_hash_key(vm, key, &hash, NULL) == 0) {
        return;
    }
    (void)__tinypy_internal_dict_insert(vm, dict, key, value, hash, TINYPY_TRUE, TINYPY_FALSE, NULL);
}
//////////////////////////////////////////////////////////////////////////
/* PyDict_SetItem */
tinypy_bool_t tinypy_internal_dict_set_hash_checked(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_hash_t hash, tinypy_error_t **out_error) {
    tinypy_bool_t stored = __tinypy_internal_dict_insert(vm, dict, key, value, hash, TINYPY_TRUE, TINYPY_TRUE, out_error);

    return stored;
}
//////////////////////////////////////////////////////////////////////////
/* insertdict stores without growing the table, for callers that sized it
   for all their entries beforehand. */
tinypy_bool_t tinypy_internal_dict_insert_checked(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_hash_t hash, tinypy_error_t **out_error) {
    tinypy_bool_t stored = __tinypy_internal_dict_insert(vm, dict, key, value, hash, TINYPY_FALSE, TINYPY_TRUE, out_error);

    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_setdefault_checked(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *default_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_hash_t hash;
    tinypy_dict_lookup_t lookup;

    if (__tinypy_internal_dict_hash_key(vm, key, &hash, out_error) == 0) {
        return NULL;
    }
    if (__tinypy_internal_dict_lookup(vm, dict, key, hash, &lookup, out_error) == 0) {
        return NULL;
    }
    if (lookup.found != 0) {
        tinypy_value_t *result = TINYPY_DICT_OBJECT(dict)->table[lookup.index].value;

        return TINYPY_RET(result);
    }
    if (__tinypy_internal_dict_store(vm, dict, key, default_value, hash, lookup.index, TINYPY_DICT_SIZE(dict), TINYPY_TRUE, TINYPY_TRUE, out_error) == 0) {
        return NULL;
    }
    return TINYPY_RET(default_value);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_iteration_error(tinypy_error_t *iteration_error, tinypy_error_t **out_error) {
    if (out_error != NULL) {
        *out_error = iteration_error;
    }
    else if (iteration_error != NULL) {
        tinypy_error_release(iteration_error);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_dict_pair_conversion_error(tinypy_vm_t *vm, size_t index, tinypy_error_t **out_error) {
    char index_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t index_size = tinypy_internal_format_size(index_buffer, index);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("cannot convert dictionary update sequence element #"),
        {index_buffer, index_size},
        TINYPY_MESSAGE_PART_LITERAL(" to a sequence")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_set_pair(tinypy_value_t *target, tinypy_value_t *pair, size_t index, const char *negative_hint_message, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_value_t *sequence;

    if (pair->type == &vm->types[TINYPY_VALUE_TUPLE] || pair->type == &vm->types[TINYPY_VALUE_LIST]) {
        sequence = TINYPY_RET(pair);
    }
    else {
        tinypy_error_t *conversion_error = NULL;
        tinypy_value_t *iterator = tinypy_iter(pair, &conversion_error);

        sequence = NULL;
        if (iterator != NULL) {
            sequence = tinypy_internal_list_from_items_checked(vm, NULL, 0U, &conversion_error);
            if (sequence != NULL && tinypy_internal_list_extend_iterable(sequence, iterator, negative_hint_message, &conversion_error) == 0) {
                TINYPY_DECREF(sequence);
                sequence = NULL;
            }
            TINYPY_DECREF(iterator);
        }
        if (sequence == NULL) {
            if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, &conversion_error) != 0) {
                __tinypy_internal_dict_pair_conversion_error(vm, index, out_error);
            }
            else {
                __tinypy_internal_dict_iteration_error(conversion_error, out_error);
            }
            return TINYPY_FALSE;
        }
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size_t size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(sequence) : TINYPY_LIST_SIZE(sequence);

    if (size != 2U) {
        char index_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        char size_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t index_size = tinypy_internal_format_size(index_buffer, index);
        size_t size_size = tinypy_internal_format_size(size_buffer, size);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("dictionary update sequence element #"),
            {index_buffer, index_size},
            TINYPY_MESSAGE_PART_LITERAL(" has length "),
            {size_buffer, size_size},
            TINYPY_MESSAGE_PART_LITERAL("; 2 is required")
        };

        TINYPY_DECREF(sequence);
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *key = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(sequence, 0U) : TINYPY_LIST_GET(sequence, 0U);
    tinypy_value_t *value = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(sequence, 1U) : TINYPY_LIST_GET(sequence, 1U);

    TINYPY_INCREF(key);
    TINYPY_INCREF(value);
    tinypy_bool_t inserted = tinypy_internal_dict_set_checked(vm, target, key, value, out_error);

    TINYPY_DECREF(key);
    TINYPY_DECREF(value);
    TINYPY_DECREF(sequence);
    return inserted;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_update_mapping(tinypy_value_t *target, tinypy_value_t *source, tinypy_value_t *keys_method, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_value_t *empty_args = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *keys = tinypy_call(keys_method, empty_args, NULL, out_error);

    TINYPY_DECREF(empty_args);
    if (keys == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *iterator = tinypy_iter(keys, out_error);
    tinypy_error_t *iteration_error = NULL;
    TINYPY_DECREF(keys);
    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);

        if (key == NULL) {
            break;
        }
        tinypy_value_t *value = tinypy_get_item(source, key, out_error);
        if (value == NULL || tinypy_internal_dict_set_checked(vm, target, key, value, out_error) == 0) {
            if (value != NULL) {
                TINYPY_DECREF(value);
            }
            TINYPY_DECREF(key);
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(value);
        TINYPY_DECREF(key);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        __tinypy_internal_dict_iteration_error(iteration_error, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* PyDict_Merge presizes for another dictionary and reuses its stored hashes;
   mappings and sequences of pairs grow the table one key at a time. */
tinypy_bool_t tinypy_internal_dict_update_from(tinypy_value_t *target, tinypy_value_t *source, const char *negative_hint_message, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);

    if (target == source) {
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_DICT) {
        if (TINYPY_DICT_SIZE(source) == 0U) {
            return TINYPY_TRUE;
        }
        if (tinypy_internal_dict_merge_reserve_checked(vm, target, TINYPY_DICT_SIZE(source), out_error) == 0) {
            return TINYPY_FALSE;
        }
        size_t index;

        for (index = 0U; index <= TINYPY_DICT_OBJECT(source)->mask; ++index) {
            tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(source)->table[index];

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                continue;
            }
            tinypy_value_t *key = entry->key;
            tinypy_value_t *value = entry->value;
            tinypy_hash_t hash = entry->hash;
            TINYPY_INCREF(key);
            TINYPY_INCREF(value);
            tinypy_bool_t inserted = tinypy_internal_dict_set_hash_checked(vm, target, key, value, hash, out_error);
            TINYPY_DECREF(value);
            TINYPY_DECREF(key);
            if (inserted == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    }

    tinypy_value_t *keys_method = NULL;
    int32_t mapping_status = tinypy_internal_object_get_optional_attr_key(source, vm->internal_keys_key, &keys_method, out_error);
    if (mapping_status < 0) {
        return TINYPY_FALSE;
    }
    if (mapping_status > 0) {
        tinypy_bool_t result = tinypy_internal_dict_update_mapping(target, source, keys_method, out_error);
        TINYPY_DECREF(keys_method);
        return result;
    }
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    tinypy_error_t *iteration_error = NULL;
    size_t index = 0U;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *pair = tinypy_next(iterator, &iteration_error);

        if (pair == NULL) {
            break;
        }
        tinypy_bool_t updated = __tinypy_internal_dict_set_pair(target, pair, index, negative_hint_message, out_error);
        TINYPY_DECREF(pair);
        if (updated == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        index += 1U;
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        __tinypy_internal_dict_iteration_error(iteration_error, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_delete_optional(tinypy_vm_t *vm, tinypy_value_t *dict, const tinypy_value_t *key) {
    tinypy_bool_t deleted;

    if (tinypy_internal_dict_delete_optional_checked(vm, dict, key, &deleted, NULL) == 0) {
        return TINYPY_FALSE;
    }
    return deleted;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_delete_optional_checked(tinypy_vm_t *vm, tinypy_value_t *dict, const tinypy_value_t *key, tinypy_bool_t *out_deleted, tinypy_error_t **out_error) {
    tinypy_dict_lookup_t lookup;
    tinypy_hash_t hash;

    if (__tinypy_internal_dict_find_public(vm, dict, key, &lookup, &hash, out_error) == 0) {
        *out_deleted = TINYPY_FALSE;
        return TINYPY_FALSE;
    }
    if (lookup.found == 0) {
        *out_deleted = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    (void)tinypy_internal_dict_delete_index(vm, dict, lookup.index, NULL, NULL);
    *out_deleted = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_delete_index(tinypy_vm_t *vm, tinypy_value_t *dict, size_t index, tinypy_value_t **out_key, tinypy_value_t **out_value) {
    tinypy_dict_entry_t *entry;
    tinypy_value_t *owned_key;
    tinypy_value_t *owned_value;

    if (index > TINYPY_DICT_OBJECT(dict)->mask) {
        return TINYPY_FALSE;
    }
    entry = &TINYPY_DICT_OBJECT(dict)->table[index];
    if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
        return TINYPY_FALSE;
    }
    owned_key = entry->key;
    owned_value = entry->value;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_delete(vm, dict, entry->key);
#endif
    TINYPY_DICT_ENTRY_MARK_DUMMY(entry, vm);
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_modified(TINYPY_DICT_OBJECT(dict)->type_owner);
    }
    TINYPY_DICT_OBJECT(dict)->used -= 1U;
    __tinypy_internal_dict_modified(dict);
    if (out_key != NULL) {
        *out_key = owned_key;
    }
    else {
        TINYPY_DECREF(owned_key);
    }
    if (out_value != NULL) {
        *out_value = owned_value;
    }
    else {
        TINYPY_DECREF(owned_value);
    }
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
    tinypy_dict_entry_t *entries;
    tinypy_dict_entry_t *release_entries;
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;
    size_t capacity;
    size_t table_size;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    entries = TINYPY_DICT_OBJECT(dict)->table;
    /* PyDict_Clear also drops a table that only holds deleted slots. */
    if (entries == TINYPY_DICT_OBJECT(dict)->small_table && TINYPY_DICT_OBJECT(dict)->fill == 0U) {
        return;
    }
    capacity = TINYPY_DICT_CAPACITY(dict);
    table_size = __tinypy_internal_dict_table_size(capacity);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_clear(vm, dict);
#endif
    if (entries == TINYPY_DICT_OBJECT(dict)->small_table) {
        release_entries = (tinypy_dict_entry_t *)tinypy_internal_vm_allocate(vm, table_size);
        (void)memcpy(release_entries, entries, table_size);
    }
    else {
        release_entries = entries;
    }
    (void)memset(TINYPY_DICT_OBJECT(dict)->small_table, 0, sizeof(TINYPY_DICT_OBJECT(dict)->small_table));
    TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
    TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
    TINYPY_DICT_OBJECT(dict)->used = 0U;
    TINYPY_DICT_OBJECT(dict)->fill = 0U;
    __tinypy_internal_dict_modified(dict);
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_modified(TINYPY_DICT_OBJECT(dict)->type_owner);
    }
    iterator = release_entries;
    iterator_end = release_entries + capacity;
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            tinypy_value_t *key = iterator->key;
            tinypy_value_t *value = iterator->value;

            TINYPY_DICT_ENTRY_MARK_EMPTY(iterator);
            TINYPY_DECREF(key);
            TINYPY_DECREF(value);
        }
        else {
            TINYPY_DICT_ENTRY_MARK_EMPTY(iterator);
        }
    }
    tinypy_internal_vm_deallocate(vm, release_entries, table_size);
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
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            *out_key = entry->key;
            *out_value = entry->value;
            return TINYPY_TRUE;
        }
    }
    *out_key = NULL;
    *out_value = NULL;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Like _PyDict_Next and set_next: the table is read again at every step, so
   callers may run code that changes the dictionary between steps. */
const tinypy_dict_entry_t *tinypy_internal_dict_next_entry(const tinypy_value_t *dict, size_t *position) {
    const tinypy_dict_object_t *object = TINYPY_DICT_OBJECT((tinypy_value_t *)dict);

    while (*position <= object->mask) {
        const tinypy_dict_entry_t *entry = &object->table[*position];

        *position += 1U;
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            return entry;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* dict_popitem and set_pop take slot 0 when it is live; otherwise the hash
   field of slot 0 holds the position where the search resumes. */
tinypy_bool_t tinypy_internal_dict_pop_entry(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t **out_key, tinypy_value_t **out_value) {
    tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(dict);
    size_t index = 0U;

    if (object->used == 0U) {
        return TINYPY_FALSE;
    }
    if (TINYPY_DICT_ENTRY_IS_ACTIVE(&object->table[0]) == 0) {
        tinypy_hash_t finger = object->table[0].hash;

        index = finger < 1 || (uint64_t)finger > (uint64_t)object->mask ? 1U : (size_t)finger;
        while (TINYPY_DICT_ENTRY_IS_ACTIVE(&object->table[index]) == 0) {
            index = index < object->mask ? index + 1U : 1U;
        }
    }
    (void)tinypy_internal_dict_delete_index(vm, dict, index, out_key, out_value);
    object->table[0].hash = (tinypy_hash_t)(index + 1U);
    return TINYPY_TRUE;
}
