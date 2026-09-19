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
static tinypy_bool_t __tinypy_internal_dict_lookup(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_hash_t hash, tinypy_dict_lookup_t *out_lookup, tinypy_error_t **out_error) {
    const tinypy_dict_entry_t *entries;
    size_t capacity;
    size_t first_dummy = SIZE_MAX;
    size_t mask;
    size_t index;
    uint64_t perturb;

restart:
    out_lookup->index = 0U;
    out_lookup->found = 0;
    entries = TINYPY_DICT_OBJECT(dict)->table;
    capacity = TINYPY_DICT_CAPACITY(dict);
    first_dummy = SIZE_MAX;
    mask = capacity - 1U;
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
            TINYPY_INCREF(stored_key);
            tinypy_bool_t compared = __tinypy_internal_dict_keys_equal(vm, stored_key, key, &equal, out_error);
            TINYPY_DECREF(stored_key);
            if (compared == 0) {
                return TINYPY_FALSE;
            }
            if (TINYPY_DICT_OBJECT(dict)->table != entries || entry->key != stored_key) {
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
    const tinypy_dict_entry_t *entries;
    size_t capacity;
    size_t index;
    uint64_t left_version;
    uint64_t right_version;

    if (left == right) {
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (TINYPY_DICT_OBJECT(left)->used != TINYPY_DICT_OBJECT(right)->used) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    entries = TINYPY_DICT_OBJECT(left)->table;
    capacity = TINYPY_DICT_CAPACITY(left);
    left_version = TINYPY_DICT_OBJECT(left)->mutation_version;
    right_version = TINYPY_DICT_OBJECT(right)->mutation_version;
    for (index = 0U; index < capacity; ++index) {
        const tinypy_dict_entry_t *entry = &entries[index];
        tinypy_dict_lookup_t lookup;
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
        if (__tinypy_internal_dict_lookup(vm, right, left_key, entry->hash, &lookup, out_error) == 0) {
            TINYPY_DECREF(left_key);
            TINYPY_DECREF(left_value);
            return TINYPY_FALSE;
        }
        if (lookup.found == 0) {
            TINYPY_DECREF(left_key);
            TINYPY_DECREF(left_value);
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        right_value = TINYPY_DICT_OBJECT(right)->table[lookup.index].value;
        TINYPY_INCREF(right_value);
        equal = left_value == right_value ? 1 : tinypy_compare_bool(left_value, right_value, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(right_value);
        TINYPY_DECREF(left_value);
        TINYPY_DECREF(left_key);
        if (equal < 0) {
            return TINYPY_FALSE;
        }
        if (equal == 0 || TINYPY_DICT_OBJECT(left)->mutation_version != left_version || TINYPY_DICT_OBJECT(right)->mutation_version != right_version) {
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
static size_t __tinypy_internal_dict_clean_index(const tinypy_value_t *dict, tinypy_hash_t hash) {
    size_t mask = TINYPY_DICT_OBJECT(dict)->mask;
    size_t index = (size_t)((uint64_t)hash & (uint64_t)mask);
    uint64_t perturb = (uint64_t)hash;

    while (TINYPY_DICT_ENTRY_IS_ACTIVE(&TINYPY_DICT_OBJECT(dict)->table[index])) {
        index = __tinypy_internal_dict_probe_next(index, perturb, mask);
        perturb >>= TINYPY_DICT_PERTURB_SHIFT;
    }
    return index;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_resize(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_capacity, tinypy_bool_t checked, tinypy_error_t **out_error) {
    tinypy_dict_entry_t *old_entries = TINYPY_DICT_OBJECT(dict)->table;
    size_t old_capacity = TINYPY_DICT_CAPACITY(dict);
    size_t new_capacity = TINYPY_DICT_INITIAL_CAPACITY;
    size_t new_size;
    size_t old_size;
    tinypy_dict_entry_t *iterator_end;

    while (new_capacity < minimum_capacity) {
        if (new_capacity > SIZE_MAX / 2U) {
            if (checked != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "dictionary is too large", out_error);
            }
            return TINYPY_FALSE;
        }
        new_capacity *= 2U;
    }
    if (new_capacity > SIZE_MAX / sizeof(tinypy_dict_entry_t)) {
        if (checked != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "dictionary is too large", out_error);
        }
        return TINYPY_FALSE;
    }
    new_size = __tinypy_internal_dict_table_size(new_capacity);

    tinypy_dict_entry_t *new_entries = checked != 0
                                           ? (tinypy_dict_entry_t *)tinypy_internal_vm_allocate_checked(vm, new_size, out_error)
                                           : (tinypy_dict_entry_t *)tinypy_internal_vm_allocate(vm, new_size);
    if (new_entries == NULL) {
        return TINYPY_FALSE;
    }
    (void)memset(new_entries, 0, new_size);

    tinypy_dict_entry_t *iterator = old_entries;
    iterator_end = old_entries + old_capacity;
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_needs_resize(const tinypy_value_t *dict) {
    size_t capacity = TINYPY_DICT_CAPACITY(dict);
    size_t fill = TINYPY_DICT_OBJECT(dict)->fill;

    return fill >= capacity - capacity / 3U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_reserve(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_used, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t current_capacity = TINYPY_DICT_CAPACITY(dict);
    size_t capacity = current_capacity;

    while (minimum_used >= capacity - capacity / 3U) {
        if (capacity > SIZE_MAX / 2U) {
            if (checked != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "dictionary is too large", out_error);
            }
            return TINYPY_FALSE;
        }
        capacity *= 2U;
    }
    if (capacity > current_capacity) {
        tinypy_bool_t return_value_1 = __tinypy_internal_dict_resize(vm, dict, capacity, checked, out_error);
        return return_value_1;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_reserve(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_used) {
    (void)__tinypy_internal_dict_reserve(vm, dict, minimum_used, TINYPY_FALSE, NULL);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dict_reserve_checked(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_used, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = __tinypy_internal_dict_reserve(vm, dict, minimum_used, TINYPY_TRUE, out_error);
    return return_value_1;
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
    TINYPY_DICT_OBJECT(dict)->table = TINYPY_DICT_OBJECT(dict)->small_table;
    TINYPY_DICT_OBJECT(dict)->mask = TINYPY_DICT_MIN_SIZE - 1U;
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
    uint64_t mutation_version = left_dict->mutation_version;
    tinypy_bool_t type_dictionary = left_dict->type_dictionary;
    tinypy_type_t *type_owner = left_dict->type_owner;

    if (left_small != 0) {
        (void)memcpy(left_small_table, left_dict->small_table, sizeof(left_small_table));
    }
    if (right_small != 0) {
        (void)memcpy(right_small_table, right_dict->small_table, sizeof(right_small_table));
    }
    left_dict->fill = right_dict->fill;
    left_dict->used = right_dict->used;
    left_dict->mask = right_dict->mask;
    left_dict->mutation_version = right_dict->mutation_version;
    left_dict->type_dictionary = right_dict->type_dictionary;
    left_dict->type_owner = right_dict->type_owner;
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
    right_dict->mutation_version = mutation_version;
    right_dict->type_dictionary = type_dictionary;
    right_dict->type_owner = type_owner;
    if (left_small != 0) {
        (void)memcpy(right_dict->small_table, left_small_table, sizeof(left_small_table));
        right_dict->table = right_dict->small_table;
    }
    else {
        right_dict->table = table;
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
tinypy_bool_t tinypy_internal_dict_get_optional_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_dict_get_optional_index_checked(vm, dict, key, NULL, out_value, out_error);
    return return_value_1;
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
    if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry) || __tinypy_internal_dict_keys_equal(vm, entry->key, key, &equal, NULL) == 0 || equal == 0) {
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
void tinypy_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);

    (void)tinypy_internal_dict_set_checked(vm, dict, key, value, NULL);
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
tinypy_bool_t tinypy_internal_dict_set_hash_checked(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_hash_t hash, tinypy_error_t **out_error) {
    tinypy_dict_lookup_t lookup;

    if (__tinypy_internal_dict_lookup(vm, dict, key, hash, &lookup, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_modified(TINYPY_DICT_OBJECT(dict)->type_owner);
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
        return TINYPY_TRUE;
    }

    if (__tinypy_internal_dict_needs_resize(dict)) {
        /* The new capacity follows the live entry count, not the old capacity,
           so a table full of deleted slots shrinks back the way dictresize
           does in Python 2.7. */
        size_t used = TINYPY_DICT_SIZE(dict) + 1U;
        size_t growth = used > 50000U ? 2U : 4U;
        size_t minimum = used > SIZE_MAX / growth ? SIZE_MAX : used * growth;

        if (__tinypy_internal_dict_resize(vm, dict, minimum, TINYPY_TRUE, out_error) == 0) {
            return TINYPY_FALSE;
        }
        lookup.index = __tinypy_internal_dict_clean_index(dict, hash);
    }

    TINYPY_INCREF(key);
    TINYPY_INCREF(value);
    tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[lookup.index];
    if (TINYPY_DICT_ENTRY_IS_EMPTY(entry)) {
        TINYPY_DICT_OBJECT(dict)->fill += 1U;
    }
    entry->hash = hash;
    entry->key = key;
    entry->value = value;
    TINYPY_DICT_OBJECT(dict)->used += 1U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_set(vm, dict, entry->key, value, 1);
#endif
    return TINYPY_TRUE;
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
static tinypy_bool_t __tinypy_internal_dict_set_pair(tinypy_value_t *target, tinypy_value_t *pair, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(pair);

    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST) {
        size_t size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(pair) : TINYPY_LIST_SIZE(pair);

        if (size != 2U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "dictionary update sequence item does not have length two", out_error);
            return TINYPY_FALSE;
        }
        tinypy_value_t *key = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(pair, 0U) : TINYPY_LIST_GET(pair, 0U);
        tinypy_value_t *value = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(pair, 1U) : TINYPY_LIST_GET(pair, 1U);
        tinypy_bool_t return_value_1 = tinypy_internal_dict_set_checked(vm, target, key, value, out_error);
        return return_value_1;
    }

    tinypy_value_t *iterator = tinypy_iter(pair, out_error);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *key = NULL;
    tinypy_value_t *value = NULL;
    tinypy_value_t *extra = NULL;
    tinypy_bool_t result = TINYPY_FALSE;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    key = tinypy_next(iterator, &iteration_error);
    if (key == NULL) {
        goto invalid_length;
    }
    value = tinypy_next(iterator, &iteration_error);
    if (value == NULL) {
        goto invalid_length;
    }
    extra = tinypy_next(iterator, &iteration_error);
    if (extra != NULL) {
        goto invalid_length;
    }
    if (iteration_error != NULL) {
        __tinypy_internal_dict_iteration_error(iteration_error, out_error);
        iteration_error = NULL;
        goto done;
    }
    result = tinypy_internal_dict_set_checked(vm, target, key, value, out_error);
    goto done;

invalid_length:
    if (iteration_error != NULL) {
        __tinypy_internal_dict_iteration_error(iteration_error, out_error);
        iteration_error = NULL;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "dictionary update sequence item does not have length two", out_error);
    }
done:
    if (extra != NULL) {
        TINYPY_DECREF(extra);
    }
    if (value != NULL) {
        TINYPY_DECREF(value);
    }
    if (key != NULL) {
        TINYPY_DECREF(key);
    }
    TINYPY_DECREF(iterator);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_dict_update_mapping(tinypy_value_t *target, tinypy_value_t *source, tinypy_value_t *keys_method, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_value_t *empty_args = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *keys = tinypy_call(keys_method, empty_args, NULL, out_error);

    TINYPY_DECREF(empty_args);
    if (keys == NULL) {
        return TINYPY_FALSE;
    }
    size_t hint = tinypy_internal_iterable_size_hint(keys);
    size_t used = TINYPY_DICT_SIZE(target);
    if (tinypy_internal_dict_reserve_checked(vm, target, hint > SIZE_MAX - used ? SIZE_MAX : used + hint, out_error) == 0) {
        TINYPY_DECREF(keys);
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
tinypy_bool_t tinypy_internal_dict_update_from(tinypy_value_t *target, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);

    if (target == source) {
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_DICT) {
        size_t used = TINYPY_DICT_SIZE(target);
        size_t source_size = TINYPY_DICT_SIZE(source);
        if (tinypy_internal_dict_reserve_checked(vm, target, source_size > SIZE_MAX - used ? SIZE_MAX : used + source_size, out_error) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(source);
        size_t capacity = TINYPY_DICT_OBJECT(source)->mask + 1U;
        size_t index;

        for (index = 0U; index < capacity; ++index) {
            tinypy_dict_entry_t *entry = &entries[index];

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
            if (TINYPY_DICT_OBJECT(source)->table != entries || TINYPY_DICT_SIZE(source) != source_size) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "dictionary changed size during update", out_error);
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    }

    tinypy_value_t *keys_method = NULL;
    int32_t mapping_status = tinypy_internal_object_get_optional_attr_key(source, vm->keys_key, &keys_method, out_error);
    if (mapping_status < 0) {
        return TINYPY_FALSE;
    }
    if (mapping_status > 0) {
        tinypy_bool_t result = __tinypy_internal_dict_update_mapping(target, source, keys_method, out_error);
        TINYPY_DECREF(keys_method);
        return result;
    }

    size_t hint = tinypy_internal_iterable_size_hint(source);
    size_t used = TINYPY_DICT_SIZE(target);
    if (tinypy_internal_dict_reserve_checked(vm, target, hint > SIZE_MAX - used ? SIZE_MAX : used + hint, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *pair = tinypy_next(iterator, &iteration_error);

        if (pair == NULL) {
            break;
        }
        tinypy_bool_t updated = __tinypy_internal_dict_set_pair(target, pair, out_error);
        TINYPY_DECREF(pair);
        if (updated == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
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
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_modified(TINYPY_DICT_OBJECT(dict)->type_owner);
    }
    owned_key = entry->key;
    owned_value = entry->value;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_dict_delete(vm, dict, entry->key);
#endif
    TINYPY_DICT_ENTRY_MARK_DUMMY(entry, vm);
    TINYPY_DICT_OBJECT(dict)->used -= 1U;
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
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
    if (TINYPY_DICT_OBJECT(dict)->used == 0U) {
        return;
    }
    if (TINYPY_DICT_OBJECT(dict)->type_dictionary != 0) {
        tinypy_internal_type_modified(TINYPY_DICT_OBJECT(dict)->type_owner);
    }

    entries = TINYPY_DICT_OBJECT(dict)->table;
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
    TINYPY_DICT_OBJECT(dict)->mutation_version += UINT64_C(1);
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
