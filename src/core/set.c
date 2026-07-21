#include "tinypy/set.h"

#include "internal.h"

#include <assert.h>

enum {
    TINYPY_SET_BINARY_AND = 0,
    TINYPY_SET_BINARY_XOR = 1,
    TINYPY_SET_BINARY_OR = 2,
    TINYPY_SET_BINARY_SUBTRACT = 3
};

//////////////////////////////////////////////////////////////////////////
static tinypy_set_object_t *__tinypy_set_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_SET || tinypy_internal_value_kind(value) == TINYPY_VALUE_FROZENSET);
    return TINYPY_SET_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_set_value_hashable(const tinypy_value_t *value) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    return kind != TINYPY_VALUE_LIST && kind != TINYPY_VALUE_DICT && kind != TINYPY_VALUE_SET;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_allocate(tinypy_vm_t *vm, int32_t frozen) {
    tinypy_value_type_e kind = frozen != 0 ? TINYPY_VALUE_FROZENSET : TINYPY_VALUE_SET;
    tinypy_set_object_t *set = (tinypy_set_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*set));

    set->dict = tinypy_dict_new(vm);
    return &set->base;
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_set_insert(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(set);
    tinypy_set_object_t *object = __tinypy_set_validate(set);
    tinypy_value_t *none;

    tinypy_internal_clear_error(out_error);
    if (__tinypy_set_value_hashable(item) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable type used as a set member", out_error);
        return INT32_C(0);
    }
    if (tinypy_dict_contains(object->dict, item) != 0) {
        return INT32_C(1);
    }
    none = tinypy_none_get(vm);
    tinypy_dict_set(object->dict, item, none);
    tinypy_release(none);
    object->hash_computed = 0;
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_set_update_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(iterable)->dict);
        size_t index;

        for (index = 0U; index <= dict->mask; index += 1U) {
            tinypy_dict_entry_t *entry = &dict->table[index];

            if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && __tinypy_set_insert(set, entry->key, out_error) == 0) {
                return INT32_C(0);
            }
        }
        return INT32_C(1);
    } {
        tinypy_error_t *iteration_error = NULL;
        tinypy_value_t *iterator = tinypy_iter(iterable, &iteration_error);

        if (iterator == NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else if (iteration_error != NULL) {
                tinypy_error_release(iteration_error);
            }
            return INT32_C(0);
        }
        for (;;) {
            tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

            if (item == NULL) {
                break;
            }
            if (__tinypy_set_insert(set, item, out_error) == 0) {
                tinypy_release(item);
                tinypy_release(iterator);
                return INT32_C(0);
            }
            tinypy_release(item);
        }
        tinypy_release(iterator);
        if (iteration_error != NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else {
                tinypy_error_release(iteration_error);
            }
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_copy_kind(const tinypy_value_t *source, int32_t frozen) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(source);
    tinypy_value_t *result = __tinypy_set_allocate(vm, frozen);
    int32_t updated = __tinypy_set_update_iterable(result, (tinypy_value_t *)source, NULL);

    assert(updated != 0);
    (void)updated;
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_normalize(tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(iterable);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        tinypy_retain(iterable);
        return iterable;
    }
    return tinypy_set_from_iterable(iterable, INT32_C(0), out_error);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_set_is_subset(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_dict_object_t *left_dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT((tinypy_value_t *)left)->dict);
    tinypy_value_t *right_dict = TINYPY_SET_OBJECT((tinypy_value_t *)right)->dict;
    size_t index;

    if (left_dict->used > tinypy_dict_size(right_dict)) {
        return INT32_C(0);
    }
    for (index = 0U; index <= left_dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &left_dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(right_dict, entry->key) == 0) {
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_set_intersection_update_set(tinypy_value_t *set, const tinypy_value_t *other) {
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(set)->dict);
    tinypy_value_t *other_dict = TINYPY_SET_OBJECT((tinypy_value_t *)other)->dict;
    size_t index;

    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(other_dict, entry->key) == 0) {
            tinypy_dict_delete(&dict->base, entry->key);
        }
    }
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_set_difference_update_set(tinypy_value_t *set, const tinypy_value_t *other) {
    tinypy_dict_object_t *other_dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT((tinypy_value_t *)other)->dict);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(set)->dict;
    size_t index;

    if (set == other) {
        tinypy_dict_clear(dict);
        TINYPY_SET_OBJECT(set)->hash_computed = 0;
        return;
    }
    for (index = 0U; index <= other_dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &other_dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(dict, entry->key) != 0) {
            tinypy_dict_delete(dict, entry->key);
        }
    }
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_set_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_SET_OBJECT(value)->dict, user_data);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    __tinypy_set_validate(value);
    return tinypy_iter(TINYPY_SET_OBJECT(value)->dict, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_set_new(tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    return __tinypy_set_allocate(vm, INT32_C(0));
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frozenset_new(tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    return __tinypy_set_allocate(vm, INT32_C(1));
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_set_from_iterable(tinypy_value_t *iterable, int32_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_value_t *result;

    assert(iterable != NULL);
    vm = tinypy_internal_value_vm(iterable);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, iterable));
    tinypy_internal_clear_error(out_error);
    if (frozen != 0 && tinypy_internal_value_kind(iterable) == TINYPY_VALUE_FROZENSET) {
        tinypy_retain(iterable);
        return iterable;
    }
    result = __tinypy_set_allocate(vm, frozen);
    if (__tinypy_set_update_iterable(result, iterable, out_error) == 0) {
        tinypy_release(result);
        return NULL;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
size_t tinypy_set_size(const tinypy_value_t *set) {
    tinypy_set_object_t *set_validate = __tinypy_set_validate(set);
    return tinypy_dict_size(set_validate->dict);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_set_contains(const tinypy_value_t *set, const tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;

    __tinypy_set_validate(set);
    assert(item != NULL);
    vm = tinypy_internal_value_vm(set);
    assert(tinypy_internal_value_belongs_to(vm, item));
    tinypy_internal_clear_error(out_error);
    if (__tinypy_set_value_hashable(item) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable type used for set lookup", out_error);
        return INT32_C(-1);
    }
    return tinypy_dict_contains(TINYPY_SET_OBJECT((tinypy_value_t *)set)->dict, item);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_set_add(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    assert(tinypy_internal_value_kind(set) == TINYPY_VALUE_SET);
    assert(item != NULL);
    assert(tinypy_internal_value_belongs_to(tinypy_internal_value_vm(set), item));
    return __tinypy_set_insert(set, item, out_error);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_set_discard(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error) {
    int32_t contains;

    assert(tinypy_internal_value_kind(set) == TINYPY_VALUE_SET);
    assert(item != NULL);
    assert(tinypy_internal_value_belongs_to(tinypy_internal_value_vm(set), item));
    contains = tinypy_set_contains(set, item, out_error);
    if (contains <= 0) {
        return contains < 0 ? INT32_C(0) : INT32_C(1);
    }
    tinypy_dict_delete(TINYPY_SET_OBJECT(set)->dict, item);
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_set_clear(tinypy_value_t *set) {
    assert(tinypy_internal_value_kind(set) == TINYPY_VALUE_SET);
    tinypy_dict_clear(TINYPY_SET_OBJECT(set)->dict);
    TINYPY_SET_OBJECT(set)->hash_computed = 0;
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_set_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_type_e left_kind = tinypy_internal_value_kind(left);
    tinypy_value_type_e right_kind = tinypy_internal_value_kind(right);

    if ((left_kind != TINYPY_VALUE_SET && left_kind != TINYPY_VALUE_FROZENSET) || (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET)) {
        return INT32_C(0);
    }
    return tinypy_set_size(left) == tinypy_set_size(right) && __tinypy_set_is_subset(left, right) != 0 ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_frozenset_hash(const tinypy_value_t *value) {
    tinypy_set_object_t *set = __tinypy_set_validate(value);
    tinypy_dict_object_t *dict;
    uint64_t hash;
    size_t index;

    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_FROZENSET);
    if (set->hash_computed != 0) {
        return set->hash;
    }
    dict = TINYPY_DICT_OBJECT(set->dict);
    hash = UINT64_C(1927868237) * ((uint64_t)dict->used + UINT64_C(1));
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            uint64_t member_hash = (uint64_t)entry->hash;
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
    tinypy_vm_t *vm = tinypy_internal_value_vm(left);
    tinypy_value_type_e left_kind = tinypy_internal_value_kind(left);
    tinypy_value_type_e right_kind = tinypy_internal_value_kind(right);
    tinypy_value_t *result;

    tinypy_internal_clear_error(out_error);
    if ((left_kind != TINYPY_VALUE_SET && left_kind != TINYPY_VALUE_FROZENSET) || (right_kind != TINYPY_VALUE_SET && right_kind != TINYPY_VALUE_FROZENSET)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set operation requires set operands", out_error);
        return NULL;
    }
    result = __tinypy_set_copy_kind(left, left_kind == TINYPY_VALUE_FROZENSET);
    if (operation == TINYPY_SET_BINARY_AND) {
        __tinypy_set_intersection_update_set(result, right);
    }
    else if (operation == TINYPY_SET_BINARY_SUBTRACT) {
        __tinypy_set_difference_update_set(result, right);
    }
    else if (operation == TINYPY_SET_BINARY_OR) {
        if (__tinypy_set_update_iterable(result, right, out_error) == 0) {
            tinypy_release(result);
            return NULL;
        }
    }
    else {
        tinypy_dict_object_t *right_dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(right)->dict);
        size_t index;

        for (index = 0U; index <= right_dict->mask; index += 1U) {
            tinypy_dict_entry_t *entry = &right_dict->table[index];

            if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) {
                continue;
            }
            if (tinypy_dict_contains(TINYPY_SET_OBJECT(result)->dict, entry->key) != 0) {
                tinypy_dict_delete(TINYPY_SET_OBJECT(result)->dict, entry->key);
            }
            else if (__tinypy_set_insert(result, entry->key, out_error) == 0) {
                tinypy_release(result);
                return NULL;
            }
        }
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_set_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = tinypy_tuple_size(args);

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set method received invalid arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_none(tinypy_vm_t *vm) {
    return tinypy_none_get(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    int condition = __tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 0U);
        tinypy_value_t *item_2 = tinypy_tuple_get(args, 1U);
        condition = tinypy_set_add(item, item_2, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_discard_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    int condition_2 = __tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0;
    if (condition_2 == 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 0U);
        tinypy_value_t *item_2 = tinypy_tuple_get(args, 1U);
        condition_2 = tinypy_set_discard(item, item_2, out_error) == 0;
    }
    if (condition_2) {
        return NULL;
    }
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_remove_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *set;
    tinypy_value_t *item;
    int32_t contains;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    set = tinypy_tuple_get(args, 0U);
    item = tinypy_tuple_get(args, 1U);
    contains = tinypy_set_contains(set, item, out_error);
    if (contains < 0) {
        return NULL;
    }
    if (contains == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "set member was not found", out_error);
        return NULL;
    }
    tinypy_dict_delete(TINYPY_SET_OBJECT(set)->dict, item);
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_clear_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    tinypy_set_clear(item);
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *set;
    tinypy_dict_object_t *dict;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    set = tinypy_tuple_get(args, 0U);
    dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(set)->dict);
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *item = entry->key;

            tinypy_retain(item);
            tinypy_dict_delete(&dict->base, item);
            return item;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "pop from an empty set", out_error);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_copy_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(self) == TINYPY_VALUE_FROZENSET) {
        tinypy_retain(self);
        return self;
    }
    return __tinypy_set_copy_kind(self, INT32_C(0));
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_union_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_value_type_e kind = tinypy_internal_value_kind(self);
    result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET);
    for (index = 1U; index < tinypy_tuple_size(args); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, index);
        if (__tinypy_set_update_iterable(result, item, out_error) == 0) {
            tinypy_release(result);
            return NULL;
        }
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_intersection_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_value_type_e kind = tinypy_internal_value_kind(self);
    result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET);
    for (index = 1U; index < tinypy_tuple_size(args); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, index);
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            tinypy_release(result);
            return NULL;
        }
        __tinypy_set_intersection_update_set(result, other);
        tinypy_release(other);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_difference_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_value_type_e kind = tinypy_internal_value_kind(self);
    result = __tinypy_set_copy_kind(self, kind == TINYPY_VALUE_FROZENSET);
    for (index = 1U; index < tinypy_tuple_size(args); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, index);
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            tinypy_release(result);
            return NULL;
        }
        __tinypy_set_difference_update_set(result, other);
        tinypy_release(other);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    tinypy_value_t *other;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_value_t *item = tinypy_tuple_get(args, 1U);
    other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    result = tinypy_internal_set_binary(self, other, TINYPY_SET_BINARY_XOR, out_error);
    tinypy_release(other);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    for (index = 1U; index < tinypy_tuple_size(args); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, index);
        if (__tinypy_set_update_iterable(self, item, out_error) == 0) {
            return NULL;
        }
    }
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_intersection_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    for (index = 1U; index < tinypy_tuple_size(args); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, index);
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            return NULL;
        }
        __tinypy_set_intersection_update_set(self, other);
        tinypy_release(other);
    }
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_difference_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    for (index = 1U; index < tinypy_tuple_size(args); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, index);
        tinypy_value_t *other = __tinypy_set_normalize(item, out_error);

        if (other == NULL) {
            return NULL;
        }
        __tinypy_set_difference_update_set(self, other);
        tinypy_release(other);
    }
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_symmetric_difference_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    tinypy_value_t *other;
    tinypy_value_t *result;
    tinypy_dict_object_t *result_dict;
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_value_t *item = tinypy_tuple_get(args, 1U);
    other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    result = tinypy_internal_set_binary(self, other, TINYPY_SET_BINARY_XOR, out_error);
    tinypy_release(other);
    if (result == NULL) {
        return NULL;
    }
    tinypy_set_clear(self);
    result_dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(result)->dict);
    for (index = 0U; index <= result_dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &result_dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            int32_t inserted = __tinypy_set_insert(self, entry->key, NULL);
            assert(inserted != 0);
            (void)inserted;
        }
    }
    tinypy_release(result);
    return __tinypy_set_none(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_issubset_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *other;
    int32_t result;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 1U);
    other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    tinypy_value_t *item_2 = tinypy_tuple_get(args, 0U);
    result = __tinypy_set_is_subset(item_2, other);
    tinypy_release(other);
    return tinypy_bool_from_i32(vm, result);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_issuperset_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *other;
    int32_t result;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 1U);
    other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    tinypy_value_t *item_2 = tinypy_tuple_get(args, 0U);
    result = __tinypy_set_is_subset(other, item_2);
    tinypy_release(other);
    return tinypy_bool_from_i32(vm, result);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_isdisjoint_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;
    tinypy_value_t *other;
    tinypy_dict_object_t *dict;
    int32_t disjoint = INT32_C(1);
    size_t index;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_value_t *item = tinypy_tuple_get(args, 1U);
    other = __tinypy_set_normalize(item, out_error);
    if (other == NULL) {
        return NULL;
    }
    dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(self)->dict);
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(TINYPY_SET_OBJECT(other)->dict, entry->key) != 0) {
            disjoint = INT32_C(0);
            break;
        }
    }
    tinypy_release(other);
    return tinypy_bool_from_i32(vm, disjoint);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    size_t size;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    size = tinypy_set_size(item);
    assert(size <= (size_t)INT64_MAX);
    return tinypy_integer_from_i64(vm, (int64_t)size);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_contains_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int32_t contains;

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    tinypy_value_t *item_2 = tinypy_tuple_get(args, 1U);
    contains = tinypy_set_contains(item, item_2, out_error);
    return contains < 0 ? NULL : tinypy_bool_from_i32(vm, contains);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_set_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    return tinypy_internal_set_iter(item, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_set_create_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || tinypy_tuple_size(args) > 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set constructor received invalid arguments", out_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 0U) {
        return __tinypy_set_allocate(vm, frozen);
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    return tinypy_set_from_iterable(item, frozen, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return __tinypy_set_create_common(type, args, kwargs, INT32_C(0), out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_frozenset_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return __tinypy_set_create_common(type, args, kwargs, INT32_C(1), out_error);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_set_type_method(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_dict_set(type->dict, key, function);
    tinypy_release(function);
    tinypy_release(key);
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
    __tinypy_set_register_common_methods(vm, &vm->set_type);
    __tinypy_set_register_common_methods(vm, &vm->frozenset_type);
    __tinypy_set_type_method(vm, &vm->set_type, "add", 3U, __tinypy_set_add_method);
    __tinypy_set_type_method(vm, &vm->set_type, "discard", 7U, __tinypy_set_discard_method);
    __tinypy_set_type_method(vm, &vm->set_type, "remove", 6U, __tinypy_set_remove_method);
    __tinypy_set_type_method(vm, &vm->set_type, "pop", 3U, __tinypy_set_pop_method);
    __tinypy_set_type_method(vm, &vm->set_type, "clear", 5U, __tinypy_set_clear_method);
    __tinypy_set_type_method(vm, &vm->set_type, "update", 6U, __tinypy_set_update_method);
    __tinypy_set_type_method(vm, &vm->set_type, "intersection_update", 19U, __tinypy_set_intersection_update_method);
    __tinypy_set_type_method(vm, &vm->set_type, "difference_update", 17U, __tinypy_set_difference_update_method);
    __tinypy_set_type_method(vm, &vm->set_type, "symmetric_difference_update", 27U, __tinypy_set_symmetric_difference_update_method);
}
