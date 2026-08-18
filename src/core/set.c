#include "tinypy/set.h"

#include "internal.h"

enum {
    TINYPY_SET_BINARY_AND = 0,
    TINYPY_SET_BINARY_XOR = 1,
    TINYPY_SET_BINARY_OR = 2,
    TINYPY_SET_BINARY_SUBTRACT = 3
};

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate(tinypy_vm_t *vm, tinypy_bool_t frozen) {
    tinypy_value_type_e kind = frozen != 0 ? TINYPY_VALUE_FROZENSET : TINYPY_VALUE_SET;
    tinypy_set_object_t *set = (tinypy_set_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*set));

    set->dict = tinypy_dict_new(vm);
    return &set->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_insert(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_set_object_t *object = TINYPY_SET_OBJECT(set);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_bool_t inserted = tinypy_internal_dict_set_checked(vm, object->dict, item, none, out_error);
    TINYPY_DECREF(none);
    if (inserted == 0) {
        return TINYPY_FALSE;
    }
    object->hash_computed = 0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_update_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *dict = TINYPY_SET_OBJECT(iterable)->dict;
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);

        for (; iterator != iterator_end; ++iterator) {
            if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && __tinypy_set_insert(set, iterator->key, out_error) == 0) {
                return TINYPY_FALSE;
            }
        }
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
static tinypy_value_t *__tinypy_set_copy_kind(const tinypy_value_t *source, tinypy_bool_t frozen) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(source);
    tinypy_value_t *result = __tinypy_set_allocate(vm, frozen);
    tinypy_bool_t updated = __tinypy_set_update_iterable(result, (tinypy_value_t *)source, NULL);

    (void)updated;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_normalize(tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        TINYPY_INCREF(iterable);
        return iterable;
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
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(right_dict, iterator->key) == 0) {
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

        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        key = entry->key;
        TINYPY_INCREF(key);
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
        if (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
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
        if (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
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
            }
        }
    }
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_set_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_SET_OBJECT(value)->dict, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = tinypy_iter(TINYPY_SET_OBJECT(value)->dict, out_error);
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
        TINYPY_INCREF(iterable);
        return iterable;
    }
    tinypy_value_t *result = __tinypy_set_allocate(vm, frozen);
    if (__tinypy_set_update_iterable(result, iterable, out_error) == 0) {
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
int32_t tinypy_set_contains(const tinypy_value_t *set, const tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_bool_t contains;

    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_dict_contains_checked(vm, TINYPY_SET_OBJECT((tinypy_value_t *)set)->dict, item, &contains, out_error) == 0) {
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

    if (tinypy_internal_dict_delete_optional_checked(vm, TINYPY_SET_OBJECT(set)->dict, item, &deleted, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (deleted != 0) {
        TINYPY_SET_OBJECT(set)->hash_computed = 0;
    }
    return TINYPY_TRUE;
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
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
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
tinypy_value_t *tinypy_internal_set_binary(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    TINYPY_CLEAR_ERROR(out_error);
    if ((left_kind != TINYPY_VALUE_SET && left_kind != TINYPY_VALUE_FROZENSET) || (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set operation requires set operands", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_set_copy_kind(left, left_kind == TINYPY_VALUE_FROZENSET);
    if (operation == TINYPY_SET_BINARY_AND) {
        if (__tinypy_set_intersection_update_set(result, right, out_error) == 0) {
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
        if (__tinypy_set_update_iterable(result, right, out_error) == 0) {
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
            if (entries[index].state != TINYPY_DICT_ENTRY_ACTIVE) {
                continue;
            }
            tinypy_value_t *key = entries[index].key;
            size_t found_index;
            tinypy_bool_t found;

            TINYPY_INCREF(key);
            if (tinypy_internal_dict_lookup_hash_checked(vm, TINYPY_SET_OBJECT(result)->dict, key, entries[index].hash, &found_index, &found, out_error) == 0) {
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
            else if (__tinypy_set_insert(result, key, out_error) == 0) {
                TINYPY_DECREF(key);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(key);
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
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
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
    if (tinypy_internal_dict_delete_optional_checked(vm, TINYPY_SET_OBJECT(set)->dict, item, &deleted, out_error) == 0) {
        return NULL;
    }
    if (deleted == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "set member was not found", out_error);
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
    iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    iterator_end = TINYPY_DICT_ITERATOR_END(dict);
    for (; iterator != iterator_end; ++iterator) {

        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            size_t index = (size_t)(iterator - TINYPY_DICT_ITERATOR_BEGIN(dict));
            tinypy_value_t *item;

            (void)tinypy_internal_dict_delete_index(vm, dict, index, &item, NULL);
            TINYPY_SET_OBJECT(set)->hash_computed = 0;
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
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_FROZENSET) {
        TINYPY_INCREF(self);
        return self;
    }
    tinypy_value_t *return_value_1 = __tinypy_set_copy_kind(self, INT32_C(0));
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
    tinypy_value_t *result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (__tinypy_set_update_iterable(result, item, out_error) == 0) {
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
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    tinypy_value_t *result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        if (__tinypy_set_intersection_update_set(result, other, out_error) == 0) {
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
    tinypy_value_t *result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        if (__tinypy_set_difference_update_set(result, other, out_error) == 0) {
            TINYPY_DECREF(other);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(other);
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
        if (__tinypy_set_update_iterable(self, item, out_error) == 0) {
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_intersection_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
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
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            return NULL;
        }
        if (__tinypy_set_intersection_update_set(self, other, out_error) == 0) {
            TINYPY_DECREF(other);
            return NULL;
        }
        TINYPY_DECREF(other);
    }
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
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            return NULL;
        }
        if (__tinypy_set_difference_update_set(self, other, out_error) == 0) {
            TINYPY_DECREF(other);
            return NULL;
        }
        TINYPY_DECREF(other);
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;
    tinypy_value_t *result_dict;
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

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
    tinypy_set_clear(self);
    result_dict = TINYPY_SET_OBJECT(result)->dict;
    iterator = TINYPY_DICT_ITERATOR_BEGIN(result_dict);
    iterator_end = TINYPY_DICT_ITERATOR_END(result_dict);
    for (; iterator != iterator_end; ++iterator) {

        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_bool_t inserted = __tinypy_set_insert(self, iterator->key, NULL);
            (void)inserted;
        }
    }
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
    tinypy_value_t *other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    dict = TINYPY_SET_OBJECT(self)->dict;
    entries = TINYPY_DICT_ITERATOR_BEGIN(dict);
    capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
    version = TINYPY_DICT_OBJECT(dict)->mutation_version;
    for (index = 0U; index < capacity; ++index) {

        if (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
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
static tinypy_value_t *__tinypy_set_create_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_bool_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) > 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set constructor received invalid arguments", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = __tinypy_set_allocate(vm, frozen);
        return return_value_1;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_2 = tinypy_set_from_iterable(item, frozen, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_set_create_common(type, args, kwargs, INT32_C(0), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_frozenset_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_set_create_common(type, args, kwargs, INT32_C(1), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_set_type_method(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_dict_set(type->dict, key, function);
    TINYPY_DECREF(function);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_set_register_common_methods(tinypy_vm_t *vm, tinypy_type_t *type) {
    __tinypy_set_type_method(vm, type, "copy", 4U, __tinypy_set_copy_method);
    __tinypy_set_type_method(vm, type, "union", 5U, __tinypy_set_union_method);
    __tinypy_set_type_method(vm, type, "intersection", 12U, __tinypy_set_intersection_method);
    __tinypy_set_type_method(vm, type, "difference", 10U, __tinypy_set_difference_method);
    __tinypy_set_type_method(vm, type, "symmetric_difference", 20U, __tinypy_set_symmetric_difference_method);
    __tinypy_set_type_method(vm, type, "issubset", 8U, __tinypy_set_issubset_method);
    __tinypy_set_type_method(vm, type, "issuperset", 10U, __tinypy_set_issuperset_method);
    __tinypy_set_type_method(vm, type, "isdisjoint", 10U, __tinypy_set_isdisjoint_method);
    __tinypy_set_type_method(vm, type, "__len__", 7U, __tinypy_set_len_method);
    __tinypy_set_type_method(vm, type, "__contains__", 12U, __tinypy_set_contains_method);
    __tinypy_set_type_method(vm, type, "__iter__", 8U, __tinypy_set_iter_method);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_set_types(tinypy_vm_t *vm) {
    __tinypy_set_register_common_methods(vm, &vm->types[TINYPY_VALUE_SET]);
    __tinypy_set_register_common_methods(vm, &vm->types[TINYPY_VALUE_FROZENSET]);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "add", 3U, __tinypy_set_add_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "discard", 7U, __tinypy_set_discard_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "remove", 6U, __tinypy_set_remove_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "pop", 3U, __tinypy_set_pop_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "clear", 5U, __tinypy_set_clear_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "update", 6U, __tinypy_set_update_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "intersection_update", 19U, __tinypy_set_intersection_update_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "difference_update", 17U, __tinypy_set_difference_update_method);
    __tinypy_set_type_method(vm, &vm->types[TINYPY_VALUE_SET], "symmetric_difference_update", 27U, __tinypy_set_symmetric_difference_update_method);
}
