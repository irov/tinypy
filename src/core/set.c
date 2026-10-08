#include "tinypy/set.h"

#include "internal.h"

enum {
    TINYPY_SET_BINARY_AND = 0,
    TINYPY_SET_BINARY_XOR = 1,
    TINYPY_SET_BINARY_OR = 2,
    TINYPY_SET_BINARY_SUBTRACT = 3
};

/* The operations follow Objects/setobject.c of Python 2.7 step by step, so
   iteration orders and hash calls match. Tables are walked by position and
   read again at every step, and each key is held while code runs that may
   change the sets involved. */

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_is_any(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate_type(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;
    tinypy_set_object_t *set = (tinypy_set_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);

    set->weakrefs = NULL;
    set->dict = tinypy_dict_new(vm);
    return &set->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate_type_checked(tinypy_type_t *type, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_set_object_t *set = (tinypy_set_object_t *)tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);

    if (set == NULL) {
        return NULL;
    }
    set->weakrefs = NULL;
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
    TINYPY_SET_OBJECT(value)->weakrefs = NULL;
    TINYPY_SET_OBJECT(value)->dict = tinypy_dict_new(TINYPY_VALUE_VM(value));
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
static tinypy_bool_t __tinypy_set_iteration_end(tinypy_error_t *iteration_error, tinypy_error_t **out_error) {
    if (iteration_error == NULL) {
        return TINYPY_TRUE;
    }
    if (out_error != NULL) {
        *out_error = iteration_error;
    }
    else {
        tinypy_error_release(iteration_error);
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* set_add_entry */
static tinypy_bool_t __tinypy_set_add_entry(tinypy_value_t *set, tinypy_value_t *key, tinypy_hash_t hash, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_bool_t added = tinypy_internal_dict_set_hash_checked(vm, TINYPY_SET_OBJECT(set)->dict, key, &vm->none_object.base, hash, out_error);

    TINYPY_SET_OBJECT(set)->hash_computed = TINYPY_FALSE;
    return added;
}
//////////////////////////////////////////////////////////////////////////
/* set_add_key */
static tinypy_bool_t __tinypy_set_add_key(tinypy_value_t *set, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_bool_t added = tinypy_internal_dict_set_checked(vm, TINYPY_SET_OBJECT(set)->dict, key, &vm->none_object.base, out_error);

    TINYPY_SET_OBJECT(set)->hash_computed = TINYPY_FALSE;
    return added;
}
//////////////////////////////////////////////////////////////////////////
/* set_contains_entry: -1 on error, otherwise whether the table holds the key. */
static int32_t __tinypy_set_contains_entry(const tinypy_value_t *dict, tinypy_value_t *key, tinypy_hash_t hash, tinypy_error_t **out_error) {
    tinypy_bool_t found;

    if (tinypy_internal_dict_lookup_hash_checked(TINYPY_VALUE_VM(dict), dict, key, hash, NULL, &found, out_error) == 0) {
        return INT32_C(-1);
    }
    return found != 0 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
/* set_discard_entry: -1 on error, otherwise whether the key was removed. */
static int32_t __tinypy_set_discard_entry(tinypy_value_t *set, tinypy_value_t *key, tinypy_hash_t hash, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;
    size_t index;
    tinypy_bool_t found;

    if (tinypy_internal_dict_lookup_hash_checked(vm, dict, key, hash, &index, &found, out_error) == 0) {
        return INT32_C(-1);
    }
    if (found == 0) {
        return INT32_C(0);
    }
    TINYPY_SET_OBJECT(set)->hash_computed = TINYPY_FALSE;
    (void)tinypy_internal_dict_delete_index(vm, dict, index, NULL, NULL);
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
/* set_merge and the dictionary branch of set_update_internal resize once,
   then add the stored entries with their hashes. */
static tinypy_bool_t __tinypy_set_merge_table(tinypy_value_t *set, tinypy_value_t *dict, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    size_t position = 0U;

    if (tinypy_internal_dict_merge_reserve_checked(vm, TINYPY_SET_OBJECT(set)->dict, TINYPY_DICT_SIZE(dict), out_error) == 0) {
        return TINYPY_FALSE;
    }
    for (;;) {
        const tinypy_dict_entry_t *entry = tinypy_internal_dict_next_entry(dict, &position);

        if (entry == NULL) {
            return TINYPY_TRUE;
        }
        tinypy_value_t *key = entry->key;
        tinypy_hash_t hash = entry->hash;
        TINYPY_INCREF(key);
        tinypy_bool_t added = __tinypy_set_add_entry(set, key, hash, out_error);
        TINYPY_DECREF(key);
        if (added == 0) {
            return TINYPY_FALSE;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* set_update_internal */
tinypy_bool_t tinypy_internal_set_update_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_error_t *iteration_error = NULL;

    if (__tinypy_set_is_any(iterable) != 0) {
        if (iterable == set || tinypy_set_size(iterable) == 0U) {
            return TINYPY_TRUE;
        }
        tinypy_bool_t merged = __tinypy_set_merge_table(set, TINYPY_SET_OBJECT(iterable)->dict, out_error);
        return merged;
    }
    if (iterable->type == &vm->types[TINYPY_VALUE_DICT]) {
        tinypy_bool_t merged = __tinypy_set_merge_table(set, iterable, out_error);
        return merged;
    }
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);

        if (key == NULL) {
            break;
        }
        tinypy_bool_t added = __tinypy_set_add_key(set, key, out_error);
        TINYPY_DECREF(key);
        if (added == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
    }
    TINYPY_DECREF(iterator);
    tinypy_bool_t finished = __tinypy_set_iteration_end(iteration_error, out_error);
    return finished;
}
//////////////////////////////////////////////////////////////////////////
/* make_new_set */
static tinypy_value_t *__tinypy_set_make(tinypy_type_t *type, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_set_allocate_type_checked(type, out_error);

    if (result == NULL) {
        return NULL;
    }
    if (iterable != NULL && tinypy_internal_set_update_iterable(result, iterable, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* set_copy */
static tinypy_value_t *__tinypy_set_copy(tinypy_value_t *set, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_set_make(set->type, set, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_normalize(tinypy_value_t *iterable, tinypy_error_t **out_error) {
    if (__tinypy_set_is_any(iterable) != 0) {
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
/* set_issubset */
tinypy_bool_t tinypy_internal_set_is_subset_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_subset, tinypy_error_t **out_error) {
    tinypy_value_t *left_dict = TINYPY_SET_OBJECT((tinypy_value_t *)left)->dict;
    tinypy_value_t *right_dict = TINYPY_SET_OBJECT((tinypy_value_t *)right)->dict;
    size_t position = 0U;

    if (TINYPY_DICT_SIZE(left_dict) > TINYPY_DICT_SIZE(right_dict)) {
        *out_subset = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    for (;;) {
        const tinypy_dict_entry_t *entry = tinypy_internal_dict_next_entry(left_dict, &position);

        if (entry == NULL) {
            *out_subset = TINYPY_TRUE;
            return TINYPY_TRUE;
        }
        tinypy_value_t *key = entry->key;
        TINYPY_INCREF(key);
        int32_t contains = __tinypy_set_contains_entry(right_dict, key, entry->hash, out_error);
        TINYPY_DECREF(key);
        if (contains <= 0) {
            *out_subset = TINYPY_FALSE;
            return contains == 0 ? TINYPY_TRUE : TINYPY_FALSE;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* Adds each entry of the walked table to the result when the probed table
   holds it, or with keep_missing when it does not. */
static tinypy_bool_t __tinypy_set_select_entries(tinypy_value_t *result, const tinypy_value_t *walked_dict, const tinypy_value_t *probed_dict, tinypy_bool_t keep_missing, tinypy_error_t **out_error) {
    size_t position = 0U;

    for (;;) {
        const tinypy_dict_entry_t *entry = tinypy_internal_dict_next_entry(walked_dict, &position);

        if (entry == NULL) {
            return TINYPY_TRUE;
        }
        tinypy_value_t *key = entry->key;
        tinypy_hash_t hash = entry->hash;
        TINYPY_INCREF(key);
        int32_t contains = __tinypy_set_contains_entry(probed_dict, key, hash, out_error);
        tinypy_bool_t selected = contains >= 0 ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_bool_t present = contains > 0 ? TINYPY_TRUE : TINYPY_FALSE;
        if (contains >= 0 && present != keep_missing) {
            selected = __tinypy_set_add_entry(result, key, hash, out_error);
        }
        TINYPY_DECREF(key);
        if (selected == 0) {
            return TINYPY_FALSE;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_intersect_iterable(tinypy_value_t *result, tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(result);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);

        if (key == NULL) {
            break;
        }
        tinypy_hash_t hash;
        tinypy_bool_t selected = tinypy_internal_dict_hash_checked(vm, key, &hash, out_error);
        if (selected != 0) {
            int32_t contains = __tinypy_set_contains_entry(TINYPY_SET_OBJECT(set)->dict, key, hash, out_error);

            selected = contains >= 0 ? TINYPY_TRUE : TINYPY_FALSE;
            if (contains > 0) {
                selected = __tinypy_set_add_entry(result, key, hash, out_error);
            }
        }
        TINYPY_DECREF(key);
        if (selected == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
    }
    TINYPY_DECREF(iterator);
    tinypy_bool_t finished = __tinypy_set_iteration_end(iteration_error, out_error);
    return finished;
}
//////////////////////////////////////////////////////////////////////////
/* set_intersection walks the smaller of two sets, the other operand on a tie;
   any other iterable has each of its elements hashed and probed. */
static tinypy_value_t *__tinypy_set_intersection(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_bool_t intersected;

    if (set == other) {
        tinypy_value_t *copy = __tinypy_set_copy(set, out_error);

        return copy;
    }
    tinypy_value_t *result = __tinypy_set_make(set->type, NULL, out_error);
    if (result == NULL) {
        return NULL;
    }
    if (__tinypy_set_is_any(other) != 0) {
        tinypy_bool_t walk_set = tinypy_set_size(other) > tinypy_set_size(set) ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_value_t *walked_dict = TINYPY_SET_OBJECT(walk_set != 0 ? set : other)->dict;
        tinypy_value_t *probed_dict = TINYPY_SET_OBJECT(walk_set != 0 ? other : set)->dict;

        intersected = __tinypy_set_select_entries(result, walked_dict, probed_dict, TINYPY_FALSE, out_error);
    }
    else {
        intersected = __tinypy_set_intersect_iterable(result, set, other, out_error);
    }
    if (intersected == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* set_intersection_update */
static tinypy_bool_t __tinypy_set_intersection_update(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_value_t *intersection = __tinypy_set_intersection(set, other, out_error);

    if (intersection == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_internal_set_swap_contents(set, intersection);
    TINYPY_DECREF(intersection);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_discard_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);

        if (key == NULL) {
            break;
        }
        tinypy_hash_t hash;
        int32_t discarded = INT32_C(-1);
        if (tinypy_internal_dict_hash_checked(vm, key, &hash, out_error) != 0) {
            discarded = __tinypy_set_discard_entry(set, key, hash, out_error);
        }
        TINYPY_DECREF(key);
        if (discarded < 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
    }
    TINYPY_DECREF(iterator);
    tinypy_bool_t finished = __tinypy_set_iteration_end(iteration_error, out_error);
    return finished;
}
//////////////////////////////////////////////////////////////////////////
/* set_difference_update_internal rebuilds the table once more than a fifth of
   it holds deleted slots. */
static tinypy_bool_t __tinypy_set_difference_update(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;

    if (set == other) {
        tinypy_set_clear(set);
        return TINYPY_TRUE;
    }
    if (__tinypy_set_is_any(other) != 0) {
        tinypy_value_t *other_dict = TINYPY_SET_OBJECT(other)->dict;
        size_t position = 0U;

        for (;;) {
            const tinypy_dict_entry_t *entry = tinypy_internal_dict_next_entry(other_dict, &position);

            if (entry == NULL) {
                break;
            }
            tinypy_value_t *key = entry->key;
            TINYPY_INCREF(key);
            int32_t discarded = __tinypy_set_discard_entry(set, key, entry->hash, out_error);
            TINYPY_DECREF(key);
            if (discarded < 0) {
                return TINYPY_FALSE;
            }
        }
    }
    else if (__tinypy_set_discard_iterable(set, other, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_dict_object_t *object = TINYPY_DICT_OBJECT(dict);
    if ((object->fill - object->used) * 5U < object->mask) {
        return TINYPY_TRUE;
    }
    size_t growth = object->used > 50000U ? 2U : 4U;
    tinypy_bool_t resized = tinypy_internal_dict_resize_checked(vm, dict, object->used * growth, out_error);
    return resized;
}
//////////////////////////////////////////////////////////////////////////
/* set_difference probes another set or an exact dictionary for each element;
   any other iterable is removed from a copy. */
static tinypy_value_t *__tinypy_set_difference(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);
    tinypy_value_t *other_dict = __tinypy_set_is_any(other) != 0 ? TINYPY_SET_OBJECT(other)->dict : (other->type == &vm->types[TINYPY_VALUE_DICT] ? other : NULL);

    if (other_dict == NULL) {
        tinypy_value_t *copy = __tinypy_set_copy(set, out_error);

        if (copy != NULL && __tinypy_set_difference_update(copy, other, out_error) == 0) {
            TINYPY_DECREF(copy);
            return NULL;
        }
        return copy;
    }
    tinypy_value_t *result = __tinypy_set_make(set->type, NULL, out_error);
    if (result == NULL) {
        return NULL;
    }
    if (__tinypy_set_select_entries(result, TINYPY_SET_OBJECT(set)->dict, other_dict, TINYPY_TRUE, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Removes each entry of the table from the set, or adds it when missing. */
static tinypy_bool_t __tinypy_set_toggle_entries(tinypy_value_t *set, tinypy_value_t *dict, tinypy_error_t **out_error) {
    size_t position = 0U;

    for (;;) {
        const tinypy_dict_entry_t *entry = tinypy_internal_dict_next_entry(dict, &position);

        if (entry == NULL) {
            return TINYPY_TRUE;
        }
        tinypy_value_t *key = entry->key;
        tinypy_hash_t hash = entry->hash;
        TINYPY_INCREF(key);
        int32_t discarded = __tinypy_set_discard_entry(set, key, hash, out_error);
        tinypy_bool_t toggled = discarded >= 0 ? TINYPY_TRUE : TINYPY_FALSE;
        if (discarded == 0) {
            toggled = __tinypy_set_add_entry(set, key, hash, out_error);
        }
        TINYPY_DECREF(key);
        if (toggled == 0) {
            return TINYPY_FALSE;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* set_symmetric_difference_update toggles the stored entries of an exact
   dictionary or a set; any other iterable becomes a set of the same type. */
static tinypy_bool_t __tinypy_set_symmetric_difference_update(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(set);

    if (set == other) {
        tinypy_set_clear(set);
        return TINYPY_TRUE;
    }
    if (other->type == &vm->types[TINYPY_VALUE_DICT]) {
        tinypy_bool_t toggled = __tinypy_set_toggle_entries(set, other, out_error);
        return toggled;
    }
    if (__tinypy_set_is_any(other) != 0) {
        tinypy_bool_t toggled = __tinypy_set_toggle_entries(set, TINYPY_SET_OBJECT(other)->dict, out_error);
        return toggled;
    }
    tinypy_value_t *other_set = __tinypy_set_make(set->type, other, out_error);
    if (other_set == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t toggled = __tinypy_set_toggle_entries(set, TINYPY_SET_OBJECT(other_set)->dict, out_error);
    TINYPY_DECREF(other_set);
    return toggled;
}
//////////////////////////////////////////////////////////////////////////
/* set_symmetric_difference starts from a set of the other operand. */
static tinypy_value_t *__tinypy_set_symmetric_difference(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_set_make(set->type, other, out_error);

    if (result != NULL && __tinypy_set_symmetric_difference_update(result, set, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* set_or */
static tinypy_value_t *__tinypy_set_union(tinypy_value_t *set, tinypy_value_t *other, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_set_copy(set, out_error);

    if (result == NULL || set == other) {
        return result;
    }
    if (tinypy_internal_set_update_iterable(result, other, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
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
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t return_value_1 = __tinypy_set_add_key(set, item, out_error);
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
/* set_richcompare: frozensets whose hashes are known and differ are unequal
   without comparing their elements. */
tinypy_bool_t tinypy_internal_set_equal_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    const tinypy_set_object_t *left_set = TINYPY_SET_OBJECT((tinypy_value_t *)left);
    const tinypy_set_object_t *right_set = TINYPY_SET_OBJECT((tinypy_value_t *)right);

    if (__tinypy_set_is_any(left) == 0 || __tinypy_set_is_any(right) == 0 || tinypy_set_size(left) != tinypy_set_size(right)
        || (left_set->hash_computed != 0 && right_set->hash_computed != 0 && left_set->hash != right_set->hash)) {
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
static tinypy_bool_t __tinypy_set_is_view(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_DICT_KEYS || kind == TINYPY_VALUE_DICT_ITEMS ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* dictviews_and and the other view operators collect the left operand into a
   set and update it in place with the right one, which may be any iterable. */
static tinypy_value_t *__tinypy_set_binary_with_view(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result = __tinypy_set_make(&vm->types[TINYPY_VALUE_SET], left, out_error);
    tinypy_bool_t updated;

    if (result == NULL) {
        return NULL;
    }
    if (operation == TINYPY_SET_BINARY_AND) {
        updated = __tinypy_set_intersection_update(result, right, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_SUBTRACT) {
        updated = __tinypy_set_difference_update(result, right, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_OR) {
        updated = tinypy_internal_set_update_iterable(result, right, out_error);
    }
    else {
        updated = __tinypy_set_symmetric_difference_update(result, right, out_error);
    }
    if (updated == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_binary(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_set_is_any(left) == 0 || __tinypy_set_is_any(right) == 0) {
        if (__tinypy_set_is_view(left) == 0 && __tinypy_set_is_view(right) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set operation requires set operands", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_1 = __tinypy_set_binary_with_view(left, right, operation, out_error);
        return return_value_1;
    }
    tinypy_value_t *result;
    if (operation == TINYPY_SET_BINARY_AND) {
        result = __tinypy_set_intersection(left, right, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_XOR) {
        result = __tinypy_set_symmetric_difference(left, right, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_OR) {
        result = __tinypy_set_union(left, right, out_error);
    }
    else {
        result = __tinypy_set_difference(left, right, out_error);
    }
    return result;
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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, mode >= 100 ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, mode >= 100 ? 0U : 1U);
    /* set_and and its siblings require sets on both sides. */
    if (__tinypy_set_is_any(left) == 0 || __tinypy_set_is_any(right) == 0) {
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
    tinypy_bool_t updated;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *other = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_set_is_any(other) == 0) {
        tinypy_value_t *result = &vm->not_implemented_object.base;

        return TINYPY_RET(result);
    }
    if (operation == TINYPY_SET_BINARY_OR) {
        updated = tinypy_internal_set_update_iterable(self, other, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_SUBTRACT) {
        updated = __tinypy_set_difference_update(self, other, out_error);
    }
    else if (operation == TINYPY_SET_BINARY_XOR) {
        updated = __tinypy_set_symmetric_difference_update(self, other, out_error);
    }
    else {
        updated = __tinypy_set_intersection_update(self, other, out_error);
    }
    if (updated == 0) {
        return NULL;
    }
    return TINYPY_RET(self);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_object_repr(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_frozenset_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
    tinypy_bool_t condition = tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0;
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
    tinypy_bool_t condition_2 = tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0;
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *set = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *key;
    tinypy_value_t *value;
    if (tinypy_internal_dict_pop_entry(vm, TINYPY_SET_OBJECT(set)->dict, &key, &value) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "pop from an empty set", out_error);
        return NULL;
    }
    TINYPY_DECREF(value);
    TINYPY_SET_OBJECT(set)->hash_computed = TINYPY_FALSE;
    return key;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_copy_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (self->type == &vm->types[TINYPY_VALUE_FROZENSET]) {
        return TINYPY_RET(self);
    }
    tinypy_value_t *return_value_1 = __tinypy_set_copy(self, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_union_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = __tinypy_set_copy(self, out_error);

    if (result == NULL) {
        return NULL;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (item != self && tinypy_internal_set_update_iterable(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* set_intersection_multi narrows the result one argument at a time. */
static tinypy_value_t *__tinypy_set_intersection_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *copy = __tinypy_set_copy(self, out_error);

        return copy;
    }
    tinypy_value_t *result = TINYPY_RET(self);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *intersection = __tinypy_set_intersection(result, *iterator, out_error);

        TINYPY_DECREF(result);
        if (intersection == NULL) {
            return NULL;
        }
        result = intersection;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* set_difference_multi */
static tinypy_value_t *__tinypy_set_difference_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *copy = __tinypy_set_copy(self, out_error);

        return copy;
    }
    tinypy_value_t *result = __tinypy_set_difference(self, TINYPY_TUPLE_GET(args, 1U), out_error);
    if (result == NULL) {
        return NULL;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 2;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        if (__tinypy_set_difference_update(result, *iterator, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_set_symmetric_difference(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
    tinypy_internal_set_swap_contents(TINYPY_TUPLE_GET(args, 0U), result);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1;
    iterator_end = TINYPY_TUPLE_ITERATOR_END(args);
    for (; iterator != iterator_end; ++iterator) {
        if (__tinypy_set_difference_update(self, *iterator, out_error) == 0) {
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    if (__tinypy_set_symmetric_difference_update(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_set_none(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_issubset_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    size_t index;
    int32_t disjoint = INT32_C(1);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    if (tinypy_set_size(item) > tinypy_set_size(self)) {
        tinypy_value_t *smaller = self;

        self = item;
        item = smaller;
    }
    tinypy_value_t *other = TINYPY_RET(self);
    dict = TINYPY_SET_OBJECT(item)->dict;
    for (index = 0U; index <= TINYPY_DICT_OBJECT(dict)->mask; ++index) {
        const tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[index];

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            tinypy_value_t *key = entry->key;
            tinypy_bool_t found;

            TINYPY_INCREF(key);
            if (tinypy_internal_dict_lookup_hash_checked(vm, TINYPY_SET_OBJECT(other)->dict, key, entry->hash, NULL, &found, out_error) == 0) {
                TINYPY_DECREF(key);
                TINYPY_DECREF(other);
                return NULL;
            }
            TINYPY_DECREF(key);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_set_iter(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_cached_empty_frozenset(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (vm->empty_frozenset == NULL) {
        vm->empty_frozenset = __tinypy_set_allocate_type_checked(&vm->types[TINYPY_VALUE_FROZENSET], out_error);
        if (vm->empty_frozenset == NULL) {
            return NULL;
        }
    }
    return TINYPY_RET(vm->empty_frozenset);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_create_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U && (type == &vm->types[TINYPY_VALUE_SET] || type == &vm->types[TINYPY_VALUE_FROZENSET])) {
        tinypy_message_part_t parts[] = {
            {type->name, type->name_size},
            TINYPY_MESSAGE_PART_LITERAL("() does not take keyword arguments"),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) > 1U) {
        tinypy_internal_make_arity_error(vm, type->name, type->name_size, TINYPY_TUPLE_SIZE(args), 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        if (type == &vm->types[TINYPY_VALUE_FROZENSET]) {
            tinypy_value_t *result = __tinypy_set_cached_empty_frozenset(vm, out_error);

            return result;
        }
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
    if (type == &vm->types[TINYPY_VALUE_FROZENSET] && tinypy_set_size(return_value_2) == 0U) {
        TINYPY_DECREF(return_value_2);
        return_value_2 = __tinypy_set_cached_empty_frozenset(vm, out_error);
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
