#include "tinypy/set.h"

#include "internal.h"

enum {
    TINYPY_SET_BINARY_AND = 0,
    TINYPY_SET_BINARY_XOR = 1,
    TINYPY_SET_BINARY_OR = 2,
    TINYPY_SET_BINARY_SUBTRACT = 3
};

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_set_like_size(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        size_t return_value_1 = tinypy_set_size(value);
        return return_value_1;
    }
    size_t return_value_2 = TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT((tinypy_value_t *)value)->dict);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate_type(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;
    tinypy_set_object_t *set = (tinypy_set_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);

    set->dict = tinypy_dict_new(vm);
    set->finger = 0U;
    return &set->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate_type_checked(tinypy_type_t *type, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_set_object_t *set = (tinypy_set_object_t *)tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);

    if (set == NULL) {
        return NULL;
    }
    set->dict = tinypy_internal_dict_new_checked(vm, out_error);
    if (set->dict == NULL) {
        TINYPY_DECREF(&set->base);
        return NULL;
    }
    return &set->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate(tinypy_vm_t *vm, tinypy_bool_t frozen) {
    tinypy_type_t *type = &vm->types[frozen != 0 ? TINYPY_VALUE_FROZENSET : TINYPY_VALUE_SET];

    tinypy_value_t *return_value_1 = __tinypy_set_allocate_type(type);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_set_initialize_empty(tinypy_value_t *value) {
    TINYPY_SET_OBJECT(value)->dict = tinypy_dict_new(TINYPY_VALUE_VM(value));
    TINYPY_SET_OBJECT(value)->finger = 0U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_set_swap_contents(tinypy_value_t *left, tinypy_value_t *right) {
    tinypy_hash_t hash = TINYPY_SET_OBJECT(left)->hash;
    tinypy_bool_t hash_computed = TINYPY_SET_OBJECT(left)->hash_computed;

    tinypy_internal_dict_swap_contents(TINYPY_SET_OBJECT(left)->dict, TINYPY_SET_OBJECT(right)->dict);
    TINYPY_SET_OBJECT(left)->hash = TINYPY_SET_OBJECT(right)->hash;
    TINYPY_SET_OBJECT(left)->hash_computed = TINYPY_SET_OBJECT(right)->hash_computed;
    TINYPY_SET_OBJECT(right)->hash = hash;
    TINYPY_SET_OBJECT(right)->hash_computed = hash_computed;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_insert(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_set_object_t *object = TINYPY_SET_OBJECT(set);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *none = TINYPY_RET_NONE(vm);
    tinypy_bool_t inserted = tinypy_internal_dict_set_checked(vm, object->dict, item, none, out_error);
    TINYPY_DECREF(none);
    if (inserted == 0) {
        return TINYPY_FALSE;
    }
    object->hash_computed = 0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_update_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        if (set == iterable) {
            return TINYPY_TRUE;
        }
        tinypy_value_t *dict = TINYPY_SET_OBJECT(iterable)->dict;
        tinypy_value_t *target_dict = TINYPY_SET_OBJECT(set)->dict;
        tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(dict);
        tinypy_value_t *none = TINYPY_RET_NONE(TINYPY_VALUE_VM(set));
        size_t target_size = TINYPY_DICT_SIZE(target_dict);
        size_t source_size = TINYPY_DICT_SIZE(dict);
        size_t capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
        size_t index;

        if (tinypy_internal_dict_reserve_checked(TINYPY_VALUE_VM(set), target_dict, source_size > SIZE_MAX - target_size ? SIZE_MAX : target_size + source_size, out_error) == 0) {
            TINYPY_DECREF(none);
            return TINYPY_FALSE;
        }

        for (index = 0U; index < capacity; ++index) {
            tinypy_dict_entry_t *entry = &entries[index];

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                continue;
            }
            tinypy_value_t *key = entry->key;
            tinypy_hash_t hash = entry->hash;
            TINYPY_INCREF(key);
            tinypy_bool_t inserted = tinypy_internal_dict_set_hash_checked(TINYPY_VALUE_VM(set), target_dict, key, none, hash, out_error);
            TINYPY_DECREF(key);
            if (inserted == 0) {
                TINYPY_DECREF(none);
                return TINYPY_FALSE;
            }
            if (TINYPY_DICT_OBJECT(dict)->table != entries || TINYPY_DICT_SIZE(dict) != source_size) {
                TINYPY_DECREF(none);
                tinypy_internal_make_vm_error(TINYPY_VALUE_VM(set), TINYPY_ERROR_RUNTIME, "set changed size during update", out_error);
                return TINYPY_FALSE;
            }
        }
        TINYPY_DECREF(none);
        TINYPY_SET_OBJECT(set)->hash_computed = 0;
        return TINYPY_TRUE;
    }
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(iterable, &iteration_error);

    if (iterator == NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else if (iteration_error != NULL) {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (__tinypy_set_insert(set, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_copy_type(const tinypy_value_t *source, tinypy_type_t *type, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_set_allocate_type(type);

    if (tinypy_internal_set_update_iterable(result, (tinypy_value_t *)source, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_copy_kind(const tinypy_value_t *source, tinypy_bool_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(source);
    tinypy_type_t *type = source->type;

    if ((frozen != 0) != (type->layout_kind == TINYPY_VALUE_FROZENSET)) {
        type = &vm->types[frozen != 0 ? TINYPY_VALUE_FROZENSET : TINYPY_VALUE_SET];
    }
    tinypy_value_t *result = __tinypy_set_copy_type(source, type, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_normalize(tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        return TINYPY_RET(iterable);
    }
    tinypy_value_t *return_value_1 = tinypy_set_from_iterable(iterable, INT32_C(0), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_is_subset(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_t *left_dict = TINYPY_SET_OBJECT((tinypy_value_t *)left)->dict;
    tinypy_value_t *right_dict = TINYPY_SET_OBJECT((tinypy_value_t *)right)->dict;

    if (TINYPY_DICT_SIZE(left_dict) > TINYPY_DICT_SIZE(right_dict)) {
        return TINYPY_FALSE;
    }
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(left_dict);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(left_dict);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) && tinypy_dict_contains(right_dict, iterator->key) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_is_subset_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_subset, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *left_dict = TINYPY_SET_OBJECT((tinypy_value_t *)left)->dict;
    tinypy_value_t *right_dict = TINYPY_SET_OBJECT((tinypy_value_t *)right)->dict;
    tinypy_dict_entry_t *entries;
    size_t capacity;
    size_t index;
    uint64_t left_version;

    if (TINYPY_DICT_SIZE(left_dict) > TINYPY_DICT_SIZE(right_dict)) {
        *out_subset = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    entries = TINYPY_DICT_ITERATOR_BEGIN(left_dict);
    capacity = TINYPY_DICT_OBJECT(left_dict)->mask + 1U;
    left_version = TINYPY_DICT_OBJECT(left_dict)->mutation_version;
    for (index = 0U; index < capacity; ++index) {
        tinypy_dict_entry_t *entry = &entries[index];
        tinypy_value_t *key;
        tinypy_bool_t found;

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            continue;
        }
        key = TINYPY_RET(entry->key);
        if (tinypy_internal_dict_lookup_hash_checked(vm, right_dict, key, entry->hash, NULL, &found, out_error) == 0) {
            TINYPY_DECREF(key);
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(key);
        if (TINYPY_DICT_OBJECT(left_dict)->mutation_version != left_version || TINYPY_DICT_OBJECT(left_dict)->table != entries) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
            return TINYPY_FALSE;
        }
        if (found == 0) {
            *out_subset = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
    }
    *out_subset = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_intersection_update_set(tinypy_value_t *set, const tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;
    tinypy_value_t *other_dict = TINYPY_SET_OBJECT((tinypy_value_t *)other)->dict;
    tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(dict);
    size_t capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
    uint64_t version = TINYPY_DICT_OBJECT(dict)->mutation_version;
    size_t index;

    for (index = 0U; index < capacity; ++index) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(&entries[index])) {
            tinypy_value_t *key = entries[index].key;
            tinypy_bool_t found;

            TINYPY_INCREF(key);
            if (tinypy_internal_dict_lookup_hash_checked(vm, other_dict, key, entries[index].hash, NULL, &found, out_error) == 0) {
                TINYPY_DECREF(key);
                return TINYPY_FALSE;
            }
            TINYPY_DECREF(key);
            if (TINYPY_DICT_OBJECT(dict)->mutation_version != version || TINYPY_DICT_OBJECT(dict)->table != entries) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                return TINYPY_FALSE;
            }
            if (found == 0) {
                (void)tinypy_internal_dict_delete_index(vm, dict, index, NULL, NULL);
                if (TINYPY_DICT_OBJECT(dict)->table != entries) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                    return TINYPY_FALSE;
                }
                version = TINYPY_DICT_OBJECT(dict)->mutation_version;
            }
        }
    }
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_difference_update_set(tinypy_value_t *set, const tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *other_dict = TINYPY_SET_OBJECT((tinypy_value_t *)other)->dict;
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;

    if (set == other) {
        tinypy_dict_clear(dict);
        TINYPY_SET_OBJECT(set)->hash_computed = 0;
        return TINYPY_TRUE;
    }
    tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(other_dict);
    size_t capacity = TINYPY_DICT_OBJECT(other_dict)->mask + 1U;
    uint64_t version = TINYPY_DICT_OBJECT(other_dict)->mutation_version;
    size_t index;

    for (index = 0U; index < capacity; ++index) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(&entries[index])) {
            tinypy_value_t *key = entries[index].key;
            size_t found_index;
            tinypy_bool_t found;

            TINYPY_INCREF(key);
            if (tinypy_internal_dict_lookup_hash_checked(vm, dict, key, entries[index].hash, &found_index, &found, out_error) == 0) {
                TINYPY_DECREF(key);
                return TINYPY_FALSE;
            }
            TINYPY_DECREF(key);
            if (TINYPY_DICT_OBJECT(other_dict)->mutation_version != version || TINYPY_DICT_OBJECT(other_dict)->table != entries) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                return TINYPY_FALSE;
            }
            if (found != 0) {
                (void)tinypy_internal_dict_delete_index(vm, dict, found_index, NULL, NULL);
                if (TINYPY_DICT_OBJECT(other_dict)->mutation_version != version || TINYPY_DICT_OBJECT(other_dict)->table != entries) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                    return TINYPY_FALSE;
                }
            }
        }
    }
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_set_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    if (TINYPY_SET_OBJECT(value)->dict != NULL) {
        visit(TINYPY_SET_OBJECT(value)->dict, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = tinypy_internal_set_iterator_new(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_set_new(tinypy_vm_t *vm) {
    tinypy_value_t *return_value_1 = __tinypy_set_allocate(vm, INT32_C(0));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frozenset_new(tinypy_vm_t *vm) {
    tinypy_value_t *return_value_1 = __tinypy_set_allocate(vm, INT32_C(1));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_set_from_iterable(tinypy_value_t *iterable, tinypy_bool_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    TINYPY_CLEAR_ERROR(out_error);
    if (frozen != 0 && TINYPY_VALUE_KIND(iterable) == TINYPY_VALUE_FROZENSET) {
        return TINYPY_RET(iterable);
    }
    tinypy_value_t *result = __tinypy_set_allocate(vm, frozen);
    if (tinypy_internal_set_update_iterable(result, iterable, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_set_size(const tinypy_value_t *set) {
    size_t return_value_1 = TINYPY_DICT_SIZE(TINYPY_SET_OBJECT((tinypy_value_t *)set)->dict);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* A mutable set is unhashable, so membership tests probe with a temporary
   frozenset the way CPython's set_contains and set_discard_key retry. */
static tinypy_value_t *__tinypy_set_probe_key(tinypy_value_t *item, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SET) {
        return TINYPY_RET(item);
    }
    tinypy_value_t *return_value_1 = tinypy_set_from_iterable(item, TINYPY_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_set_contains(const tinypy_value_t *set, const tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_bool_t contains;

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *probe = __tinypy_set_probe_key((tinypy_value_t *)item, out_error);

    if (probe == NULL) {
        return INT32_C(-1);
    }
    tinypy_bool_t checked = tinypy_internal_dict_contains_checked(vm, TINYPY_SET_OBJECT((tinypy_value_t *)set)->dict, probe, &contains, out_error);

    TINYPY_DECREF(probe);
    if (checked == 0) {
        return INT32_C(-1);
    }
    return contains != 0 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_set_add(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = __tinypy_set_insert(set, item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_set_discard(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_bool_t deleted;

    tinypy_value_t *probe = __tinypy_set_probe_key(item, out_error);

    if (probe == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t removed = tinypy_internal_dict_delete_optional_checked(vm, TINYPY_SET_OBJECT(set)->dict, probe, &deleted, out_error);

    TINYPY_DECREF(probe);
    if (removed == 0) {
        return TINYPY_FALSE;
    }
    if (deleted != 0) {
        TINYPY_SET_OBJECT(set)->hash_computed = 0;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_difference_update_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        tinypy_bool_t result = __tinypy_set_difference_update_set(set, iterable, out_error);
        return result;
    }
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_bool_t deleted;

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_dict_delete_optional_checked(TINYPY_VALUE_VM(set), TINYPY_SET_OBJECT(set)->dict, item, &deleted, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        if (deleted != 0) {
            TINYPY_SET_OBJECT(set)->hash_computed = 0;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_intersection_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *result = __tinypy_set_allocate_type_checked(set->type, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (result == NULL) {
        return NULL;
    }
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

    if (iterator == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_bool_t found;

        if (item == NULL) {
            break;
        }
        tinypy_hash_t hash = tinypy_internal_hash_value(item, out_error);

        if (tinypy_vm_has_error(vm) != 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
        if (tinypy_internal_dict_lookup_hash_checked(vm, TINYPY_SET_OBJECT(set)->dict, item, hash, NULL, &found, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
        if (found != 0 && tinypy_internal_dict_set_hash_checked(vm, TINYPY_SET_OBJECT(result)->dict, item, &vm->none_object.base, hash, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_difference_copy(tinypy_value_t *set, tinypy_value_t *other_dict, tinypy_type_t *result_type, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;
    tinypy_value_t *result = __tinypy_set_allocate_type_checked(result_type, out_error);

    if (result == NULL) {
        return NULL;
    }
    TINYPY_INCREF(dict);
    TINYPY_INCREF(other_dict);
    for (size_t index = 0U; index <= TINYPY_DICT_OBJECT(dict)->mask; ++index) {
        const tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[index];
        tinypy_bool_t found;

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            continue;
        }
        tinypy_hash_t hash = entry->hash;
        tinypy_value_t *key = TINYPY_RET(entry->key);

        if (tinypy_internal_dict_lookup_hash_checked(vm, other_dict, key, hash, NULL, &found, out_error) == 0) {
            TINYPY_DECREF(key);
            TINYPY_DECREF(other_dict);
            TINYPY_DECREF(dict);
            TINYPY_DECREF(result);
            return NULL;
        }
        if (found == 0 && tinypy_internal_dict_set_hash_checked(vm, TINYPY_SET_OBJECT(result)->dict, key, &vm->none_object.base, hash, out_error) == 0) {
            TINYPY_DECREF(key);
            TINYPY_DECREF(other_dict);
            TINYPY_DECREF(dict);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(key);
    }
    TINYPY_DECREF(other_dict);
    TINYPY_DECREF(dict);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_set_clear(tinypy_value_t *set) {
    tinypy_dict_clear(TINYPY_SET_OBJECT(set)->dict);
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if ((left_kind != TINYPY_VALUE_SET && left_kind != TINYPY_VALUE_FROZENSET) || (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET)) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = tinypy_set_size(left) == tinypy_set_size(right) && __tinypy_set_is_subset(left, right) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_equal_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if ((left_kind != TINYPY_VALUE_SET && left_kind != TINYPY_VALUE_FROZENSET) || (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET)) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    if (tinypy_set_size(left) != tinypy_set_size(right)) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_set_is_subset_checked(left, right, out_equal, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_frozenset_hash(const tinypy_value_t *value) {
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;
    uint64_t hash;

    tinypy_set_object_t *set = TINYPY_SET_OBJECT((tinypy_value_t *)value);
    if (set->hash_computed != 0) {
        return set->hash;
    }
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(set->dict);
    hash = UINT64_C(1927868237) * ((uint64_t)dict->used + UINT64_C(1));
    iterator = TINYPY_DICT_ITERATOR_BEGIN(set->dict);
    iterator_end = TINYPY_DICT_ITERATOR_END(set->dict);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            uint64_t member_hash = (uint64_t)iterator->hash;
            hash ^= (member_hash ^ (member_hash << 16U) ^ UINT64_C(89869747)) * UINT64_C(3644798167);
        }
    }
    hash = hash * UINT64_C(69069) + UINT64_C(907133923);
    if (hash == UINT64_MAX) {
        hash = UINT64_C(590923713);
    }
    set->hash = (tinypy_hash_t)hash;
    set->hash_computed = 1;
    return set->hash;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_binary_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_bool_t *out_contains, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    int32_t contains = kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET
                           ? tinypy_set_contains(value, item, out_error)
                           : tinypy_internal_dict_view_contains(value, item, out_error);

    if (contains < 0) {
        return TINYPY_FALSE;
    }
    *out_contains = contains != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_binary_update_selected(tinypy_value_t *result, tinypy_value_t *source, tinypy_value_t *other, int32_t selection, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_bool_t selected = TINYPY_TRUE;

        if (item == NULL) {
            break;
        }
        if (selection != 0) {
            tinypy_bool_t contains;

            if (__tinypy_set_binary_contains(other, item, &contains, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(iterator);
                return TINYPY_FALSE;
            }
            selected = selection > 0 ? contains : (contains == 0 ? TINYPY_TRUE : TINYPY_FALSE);
        }
        if (selected != 0 && tinypy_set_add(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_binary_with_view(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result = tinypy_set_new(vm);
    tinypy_bool_t success;

    if (operation == TINYPY_SET_BINARY_AND) {
        tinypy_value_t *selected = __tinypy_set_like_size(left) < __tinypy_set_like_size(right) ? left : right;
        tinypy_value_t *other = selected == left ? right : left;

        success = __tinypy_set_binary_update_selected(result, selected, other, 1, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_SUBTRACT) {
        success = __tinypy_set_binary_update_selected(result, left, right, -1, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_OR) {
        success = __tinypy_set_binary_update_selected(result, left, NULL, 0, out_error) != 0 && __tinypy_set_binary_update_selected(result, right, NULL, 0, out_error) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    else {
        success = __tinypy_set_binary_update_selected(result, left, right, -1, out_error) != 0 && __tinypy_set_binary_update_selected(result, right, left, -1, out_error) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    if (success == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_binary(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    TINYPY_CLEAR_ERROR(out_error);
    if ((left_kind != TINYPY_VALUE_SET && left_kind != TINYPY_VALUE_FROZENSET && left_kind != TINYPY_VALUE_DICT_KEYS && left_kind != TINYPY_VALUE_DICT_ITEMS) || (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET && right_kind != TINYPY_VALUE_DICT_KEYS && right_kind != TINYPY_VALUE_DICT_ITEMS)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set operation requires set operands", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_DICT_KEYS || left_kind == TINYPY_VALUE_DICT_ITEMS || right_kind == TINYPY_VALUE_DICT_KEYS || right_kind == TINYPY_VALUE_DICT_ITEMS) {
        tinypy_value_t *return_value_1 = __tinypy_set_binary_with_view(left, right, operation, out_error);
        return return_value_1;
    }
    tinypy_value_t *copy_source = operation == TINYPY_SET_BINARY_AND && tinypy_set_size(right) <= tinypy_set_size(left) ? right : left;
    tinypy_value_t *result = __tinypy_set_copy_type(copy_source, left->type, out_error);
    if (result == NULL) {
        return NULL;
    }
    if (operation == TINYPY_SET_BINARY_AND) {
        tinypy_value_t *other = copy_source == left ? right : left;

        if (__tinypy_set_intersection_update_set(result, other, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    else if (operation == TINYPY_SET_BINARY_SUBTRACT) {
        if (__tinypy_set_difference_update_set(result, right, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    else if (operation == TINYPY_SET_BINARY_OR) {
        if (tinypy_internal_set_update_iterable(result, right, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    else {
        tinypy_value_t *right_dict = TINYPY_SET_OBJECT(right)->dict;
        tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(right_dict);
        size_t capacity = TINYPY_DICT_OBJECT(right_dict)->mask + 1U;
        uint64_t version = TINYPY_DICT_OBJECT(right_dict)->mutation_version;
        size_t index;

        for (index = 0U; index < capacity; ++index) {
            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(&entries[index])) {
                continue;
            }
            tinypy_value_t *key = entries[index].key;
            tinypy_hash_t hash = entries[index].hash;
            size_t found_index;
            tinypy_bool_t found;

            TINYPY_INCREF(key);
            if (tinypy_internal_dict_lookup_hash_checked(vm, TINYPY_SET_OBJECT(result)->dict, key, hash, &found_index, &found, out_error) == 0) {
                TINYPY_DECREF(key);
                TINYPY_DECREF(result);
                return NULL;
            }
            if (TINYPY_DICT_OBJECT(right_dict)->mutation_version != version || TINYPY_DICT_OBJECT(right_dict)->table != entries) {
                TINYPY_DECREF(key);
                TINYPY_DECREF(result);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                return NULL;
            }
            if (found != 0) {
                (void)tinypy_internal_dict_delete_index(vm, TINYPY_SET_OBJECT(result)->dict, found_index, NULL, NULL);
            }
            else if (tinypy_internal_dict_set_hash_checked(vm, TINYPY_SET_OBJECT(result)->dict, key, &vm->none_object.base, hash, out_error) == 0) {
                TINYPY_DECREF(key);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(key);
            if (TINYPY_DICT_OBJECT(right_dict)->mutation_version != version || TINYPY_DICT_OBJECT(right_dict)->table != entries) {
                TINYPY_DECREF(result);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                return NULL;
            }
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_none(tinypy_vm_t *vm) {
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_binary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, mode >= 100 ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, mode >= 100 ? 0U : 1U);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    if (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *result = &vm->not_implemented_object.base;

        return TINYPY_RET(result);
    }
    tinypy_value_t *return_value_1 = tinypy_internal_set_binary(left, right, (int32_t)(mode % 100), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_inplace_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t operation = (int32_t)(intptr_t)user_data;

    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *other = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e other_kind = TINYPY_VALUE_KIND(other);
    if (other_kind != TINYPY_VALUE_SET && other_kind != TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *result = &vm->not_implemented_object.base;

        return TINYPY_RET(result);
    }
    tinypy_bool_t updated;

    if (operation == TINYPY_SET_BINARY_OR) {
        updated = tinypy_internal_set_update_iterable(self, other, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_SUBTRACT) {
        updated = __tinypy_set_difference_update_set(self, other, out_error);
    }
    else {
        tinypy_value_t *result = tinypy_internal_set_binary(self, other, operation, out_error);

        updated = result != NULL ? TINYPY_TRUE : TINYPY_FALSE;
        if (result != NULL) {
            tinypy_internal_set_swap_contents(self, result);
            TINYPY_DECREF(result);
        }
    }
    if (updated == 0) {
        return NULL;
    }
    TINYPY_SET_OBJECT(self)->hash_computed = 0;
    return TINYPY_RET(self);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_object_repr(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_frozenset_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_hash_t hash = tinypy_internal_frozenset_hash(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    tinypy_bool_t condition = __tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition = tinypy_set_add(item, item_2, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_discard_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    tinypy_bool_t condition_2 = __tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0;
    if (condition_2 == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition_2 = tinypy_set_discard(item, item_2, out_error) == 0;
    }
    if (condition_2) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_remove_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_bool_t deleted;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *set = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *probe = __tinypy_set_probe_key(item, out_error);

    if (probe == NULL) {
        return NULL;
    }
    tinypy_bool_t removed = tinypy_internal_dict_delete_optional_checked(vm, TINYPY_SET_OBJECT(set)->dict, probe, &deleted, out_error);

    TINYPY_DECREF(probe);
    if (removed == 0) {
        return NULL;
    }
    if (deleted == 0) {
        tinypy_internal_exception_raise_key_error(vm, item, out_error);
        return NULL;
    }
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_clear_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_set_clear(item);
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *set = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;
    size_t capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
    size_t finger = TINYPY_SET_OBJECT(set)->finger < capacity ? TINYPY_SET_OBJECT(set)->finger : 0U;
    size_t scanned;

    iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    iterator_end = iterator + capacity;
    (void)iterator_end;
    for (scanned = 0U; scanned < capacity; ++scanned) {
        size_t index = (finger + scanned) % capacity;

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(&iterator[index])) {
            tinypy_value_t *item;

            (void)tinypy_internal_dict_delete_index(vm, dict, index, &item, NULL);
            TINYPY_SET_OBJECT(set)->hash_computed = 0;
            TINYPY_SET_OBJECT(set)->finger = index + 1U;
            return item;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "pop from an empty set", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_copy_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (self->type == &vm->types[TINYPY_VALUE_FROZENSET]) {
        return TINYPY_RET(self);
    }
    tinypy_value_t *return_value_1 = __tinypy_set_copy_kind(self, TINYPY_VALUE_KIND(self) == TINYPY_VALUE_FROZENSET, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_union_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    tinypy_value_t *result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET, out_error);

    if (result == NULL) {
        return NULL;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (tinypy_internal_set_update_iterable(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_intersection_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *copy = __tinypy_set_copy_kind(self, TINYPY_VALUE_KIND(self) == TINYPY_VALUE_FROZENSET, out_error);

        return copy;
    }
    tinypy_value_t *result = TINYPY_RET(self);

    if (result == NULL) {
        return NULL;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(item);

        if (kind != TINYPY_VALUE_SET && kind != TINYPY_VALUE_FROZENSET) {
            tinypy_value_t *replacement = __tinypy_set_intersection_iterable(result, item, out_error);

            TINYPY_DECREF(result);
            if (replacement == NULL) {
                return NULL;
            }
            result = replacement;
            continue;
        }
        tinypy_value_t *copy = __tinypy_set_copy_type(result, result->type, out_error);

        if (copy == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(result);
        result = copy;
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        if (tinypy_set_size(other) <= tinypy_set_size(result)) {
            tinypy_value_t *replacement = __tinypy_set_copy_type(other, result->type, out_error);

            if (replacement == NULL) {
                TINYPY_DECREF(other);
                TINYPY_DECREF(result);
                return NULL;
            }
            if (__tinypy_set_intersection_update_set(replacement, result, out_error) == 0) {
                TINYPY_DECREF(replacement);
                TINYPY_DECREF(other);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(result);
            result = replacement;
        }
        else if (__tinypy_set_intersection_update_set(result, other, out_error) == 0) {
            TINYPY_DECREF(other);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(other);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_difference_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    tinypy_value_t *result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET, out_error);

    if (result == NULL) {
        return NULL;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        tinypy_value_type_e item_kind = TINYPY_VALUE_KIND(item);

        if (iterator == TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1 &&
            (item_kind == TINYPY_VALUE_SET || item_kind == TINYPY_VALUE_FROZENSET || item->type == &vm->types[TINYPY_VALUE_DICT])) {
            tinypy_value_t *other_dict = item_kind == TINYPY_VALUE_DICT ? item : TINYPY_SET_OBJECT(item)->dict;
            tinypy_value_t *replacement = __tinypy_set_difference_copy(self, other_dict, result->type, out_error);

            TINYPY_DECREF(result);
            if (replacement == NULL) {
                return NULL;
            }
            result = replacement;
            continue;
        }
        if (__tinypy_set_difference_update_iterable(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    result = tinypy_internal_set_binary(self, other, TINYPY_SET_BINARY_XOR, out_error);
    TINYPY_DECREF(other);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (tinypy_internal_set_update_iterable(self, item, out_error) == 0) {
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_intersection_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result = __tinypy_set_intersection_method(function, args, kwargs, user_data, out_error);

    if (result == NULL) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);

    tinypy_internal_set_swap_contents(self, result);
    TINYPY_DECREF(result);
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_difference_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (__tinypy_set_difference_update_iterable(self, item, out_error) == 0) {
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;
    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    result = tinypy_internal_set_binary(self, other, TINYPY_SET_BINARY_XOR, out_error);
    TINYPY_DECREF(other);
    if (result == NULL) {
        return NULL;
    }
    tinypy_internal_set_swap_contents(self, result);
    TINYPY_DECREF(result);
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_issubset_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_bool_t subset;
    if (tinypy_internal_set_is_subset_checked(item_2, other, &subset, out_error) == 0) {
        TINYPY_DECREF(other);
        return NULL;
    }
    result = subset != 0 ? INT32_C(1) : INT32_C(0);
    TINYPY_DECREF(other);
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_issuperset_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_bool_t subset;
    if (tinypy_internal_set_is_subset_checked(other, item_2, &subset, out_error) == 0) {
        TINYPY_DECREF(other);
        return NULL;
    }
    result = subset != 0 ? INT32_C(1) : INT32_C(0);
    TINYPY_DECREF(other);
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_isdisjoint_iterable(tinypy_value_t *self, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_value_t *iterator = tinypy_iter(source, out_error);

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_error_t *iteration_error = NULL;
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);
        tinypy_bool_t found;

        if (key == NULL) {
            break;
        }
        tinypy_bool_t checked = tinypy_internal_dict_contains_checked(vm, TINYPY_SET_OBJECT(self)->dict, key, &found, out_error);

        TINYPY_DECREF(key);
        if (checked == 0 || found != 0) {
            TINYPY_DECREF(iterator);
            if (checked == 0) {
                return NULL;
            }
            tinypy_value_t *result = TINYPY_RET_FALSE(vm);

            return result;
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_TRUE(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_isdisjoint_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *dict;
    tinypy_dict_entry_t *entries;
    size_t capacity;
    uint64_t version;
    size_t index;
    int32_t disjoint = INT32_C(1);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (self == item) {
        tinypy_value_t *result = tinypy_bool_from_i32(vm, tinypy_set_size(self) == 0U ? 1 : 0);

        return result;
    }
    if (item->type != &vm->types[TINYPY_VALUE_SET] && item->type != &vm->types[TINYPY_VALUE_FROZENSET]) {
        tinypy_value_t *result = __tinypy_set_isdisjoint_iterable(self, item, out_error);

        return result;
    }
    if (tinypy_set_size(self) > tinypy_set_size(item)) {
        tinypy_value_t *smaller = item;

        item = self;
        self = smaller;
    }
    tinypy_value_t *other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    dict = TINYPY_SET_OBJECT(self)->dict;
    entries = TINYPY_DICT_ITERATOR_BEGIN(dict);
    capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
    version = TINYPY_DICT_OBJECT(dict)->mutation_version;
    for (index = 0U; index < capacity; ++index) {

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(&entries[index])) {
            tinypy_value_t *key = entries[index].key;
            tinypy_bool_t found;

            TINYPY_INCREF(key);
            if (tinypy_internal_dict_lookup_hash_checked(vm, TINYPY_SET_OBJECT(other)->dict, key, entries[index].hash, NULL, &found, out_error) == 0) {
                TINYPY_DECREF(key);
                TINYPY_DECREF(other);
                return NULL;
            }
            TINYPY_DECREF(key);
            if (TINYPY_DICT_OBJECT(dict)->mutation_version != version || TINYPY_DICT_OBJECT(dict)->table != entries) {
                TINYPY_DECREF(other);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
                return NULL;
            }
            if (found != 0) {
                disjoint = INT32_C(0);
                break;
            }
        }
    }
    TINYPY_DECREF(other);
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, disjoint);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    size = tinypy_set_size(item);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_contains_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t contains;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    contains = tinypy_set_contains(item, item_2, out_error);
    tinypy_value_t *return_value_1 = contains < 0 ? NULL : tinypy_bool_from_i32(vm, contains);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_set_iter(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_create_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) > 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set constructor received invalid arguments", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = __tinypy_set_allocate_type_checked(type, out_error);
        return return_value_1;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (type == &vm->types[TINYPY_VALUE_FROZENSET] && TINYPY_VALUE_KIND(item) == TINYPY_VALUE_FROZENSET && item->type == type) {
        return TINYPY_RET(item);
    }
    tinypy_value_t *return_value_2 = __tinypy_set_allocate_type_checked(type, out_error);
    if (return_value_2 == NULL) {
        return NULL;
    }
    if (tinypy_internal_set_update_iterable(return_value_2, item, out_error) == 0) {
        TINYPY_DECREF(return_value_2);
        return NULL;
    }
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_set_create_common(type, args, kwargs, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_frozenset_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_set_create_common(type, args, kwargs, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_set_register_common_methods(tinypy_vm_t *vm, tinypy_type_t *type) {
    tinypy_internal_type_add_method(type, vm->internal_copy_key, __tinypy_set_copy_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_union_key, __tinypy_set_union_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_intersection_key, __tinypy_set_intersection_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_difference_key, __tinypy_set_difference_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_symmetric_difference_key, __tinypy_set_symmetric_difference_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_issubset_key, __tinypy_set_issubset_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_issuperset_key, __tinypy_set_issuperset_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_isdisjoint_key, __tinypy_set_isdisjoint_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_length_key, __tinypy_set_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_contains_key, __tinypy_set_contains_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_METHOD);
    tinypy_internal_type_add_method(type, vm->internal_special_iter_key, __tinypy_set_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_repr_key, __tinypy_set_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_and_key, __tinypy_set_binary_method, (void *)(intptr_t)TINYPY_SET_BINARY_AND, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_rand_key, __tinypy_set_binary_method, (void *)(intptr_t)(100 + TINYPY_SET_BINARY_AND), NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_xor_key, __tinypy_set_binary_method, (void *)(intptr_t)TINYPY_SET_BINARY_XOR, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_rxor_key, __tinypy_set_binary_method, (void *)(intptr_t)(100 + TINYPY_SET_BINARY_XOR), NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_or_key, __tinypy_set_binary_method, (void *)(intptr_t)TINYPY_SET_BINARY_OR, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_ror_key, __tinypy_set_binary_method, (void *)(intptr_t)(100 + TINYPY_SET_BINARY_OR), NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_sub_key, __tinypy_set_binary_method, (void *)(intptr_t)TINYPY_SET_BINARY_SUBTRACT, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_rsub_key, __tinypy_set_binary_method, (void *)(intptr_t)(100 + TINYPY_SET_BINARY_SUBTRACT), NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_set_types(tinypy_vm_t *vm) {
    __tinypy_set_register_common_methods(vm, &vm->types[TINYPY_VALUE_SET]);
    __tinypy_set_register_common_methods(vm, &vm->types[TINYPY_VALUE_FROZENSET]);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_add_key, __tinypy_set_add_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_discard_key, __tinypy_set_discard_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_remove_key, __tinypy_set_remove_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_pop_key, __tinypy_set_pop_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_clear_key, __tinypy_set_clear_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_update_key, __tinypy_set_update_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_intersection_update_key, __tinypy_set_intersection_update_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_difference_update_key, __tinypy_set_difference_update_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_symmetric_difference_update_key, __tinypy_set_symmetric_difference_update_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_special_iand_key, __tinypy_set_inplace_method, (void *)(intptr_t)TINYPY_SET_BINARY_AND, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_special_ixor_key, __tinypy_set_inplace_method, (void *)(intptr_t)TINYPY_SET_BINARY_XOR, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_special_ior_key, __tinypy_set_inplace_method, (void *)(intptr_t)TINYPY_SET_BINARY_OR, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_special_isub_key, __tinypy_set_inplace_method, (void *)(intptr_t)TINYPY_SET_BINARY_SUBTRACT, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FROZENSET]), vm->internal_special_hash_key, __tinypy_frozenset_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_value_t *hash_key = vm->internal_special_hash_key;
    tinypy_dict_set(vm->types[TINYPY_VALUE_SET].dict, hash_key, &vm->none_object.base);
}
