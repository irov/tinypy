#include "tinypy/type.h"

#include "internal.h"

#include <string.h>
typedef struct tinypy_mro_sequence_t {
    tinypy_type_t **items;
    size_t size;
    size_t position;
} tinypy_mro_sequence_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_native_wrapper_slot_t {
    const char *name;
    size_t size;
} tinypy_native_wrapper_slot_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_native_descriptor_is_wrapper(const uint8_t *bytes, size_t size) {
#define TINYPY_NATIVE_WRAPPER_SLOT(Name) {Name, sizeof(Name) - 1U}
    static const tinypy_native_wrapper_slot_t slots[] = {
        TINYPY_NATIVE_WRAPPER_SLOT("__cmp__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__repr__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__hash__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__call__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__str__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__getattribute__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__setattr__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__delattr__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__lt__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__le__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__eq__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ne__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__gt__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ge__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__iter__"),
        TINYPY_NATIVE_WRAPPER_SLOT("next"),
        TINYPY_NATIVE_WRAPPER_SLOT("__get__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__set__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__delete__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__init__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__add__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__radd__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__sub__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rsub__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__mul__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rmul__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__div__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rdiv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__mod__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rmod__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__divmod__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rdivmod__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__pow__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rpow__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__neg__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__pos__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__abs__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__nonzero__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__invert__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__lshift__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rlshift__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rshift__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rrshift__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__and__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rand__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__xor__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rxor__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__or__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ror__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__coerce__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__int__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__long__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__float__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__oct__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__hex__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__iadd__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__isub__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__imul__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__idiv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__imod__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ipow__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ilshift__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__irshift__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__iand__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ixor__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ior__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__floordiv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rfloordiv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__truediv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__rtruediv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__ifloordiv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__itruediv__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__index__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__len__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__getitem__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__setitem__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__delitem__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__getslice__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__setslice__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__delslice__"),
        TINYPY_NATIVE_WRAPPER_SLOT("__contains__")};
#undef TINYPY_NATIVE_WRAPPER_SLOT
    size_t index;

    for (index = 0U; index != sizeof(slots) / sizeof(slots[0]); ++index) {
        if (size == slots[index].size && memcmp(bytes, slots[index].name, size) == 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_error(tinypy_vm_t *vm, const char *message, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_type_set_name(tinypy_type_t *type, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t name_size;
    const uint8_t *name;

    TINYPY_CLEAR_ERROR(out_error);
    if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type __name__ must be a string", out_error);
        return TINYPY_FALSE;
    }
    name = TINYPY_TEXT_BYTES(value);
    name_size = TINYPY_TEXT_BYTE_SIZE(value);
    if (name_size != 0U && memchr(name, '\0', name_size) != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "type name must not contain null characters", out_error);
        return TINYPY_FALSE;
    }
    TINYPY_INCREF(value);
    if (type->name_object != NULL) {
        TINYPY_DECREF(type->name_object);
    }
    type->name_object = value;
    type->name = (const char *)name;
    type->name_size = name_size;
    tinypy_internal_type_modified(type);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_type_has_mutable_builtin_layout(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_BYTEARRAY ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_type_has_container_builtin_layout(tinypy_value_type_e kind) {
    tinypy_bool_t return_value_1 = __tinypy_internal_type_has_mutable_builtin_layout(kind) != 0 || kind == TINYPY_VALUE_FROZENSET ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_type_has_variable_immutable_builtin_layout(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_LONG ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_type_has_fixed_builtin_layout(const tinypy_vm_t *vm, tinypy_value_type_e kind) {
    const tinypy_type_t *builtin_type;

    if (kind == TINYPY_VALUE_INVALID || kind == TINYPY_VALUE_TYPE || kind == TINYPY_VALUE_INSTANCE || kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_WEAKREF || kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        return TINYPY_FALSE;
    }
    if (__tinypy_internal_type_has_container_builtin_layout(kind) != 0 || __tinypy_internal_type_has_variable_immutable_builtin_layout(kind) != 0) {
        return TINYPY_FALSE;
    }
    builtin_type = &vm->types[kind];
    return builtin_type->item_size == 0U && builtin_type->basic_size > sizeof(tinypy_value_t) ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_builtin_subclass_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    switch (value->type->layout_kind) {
    case TINYPY_VALUE_LIST:
        tinypy_internal_list_release_references(value, visit, user_data);
        break;
    case TINYPY_VALUE_DICT:
        tinypy_internal_dict_release_references(value, visit, user_data);
        break;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        tinypy_internal_set_release_references(value, visit, user_data);
        break;
    default:
        break;
    }
    tinypy_internal_instance_release_references(value, visit, user_data);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_fixed_builtin_subclass_slots_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    size_t index;

    for (index = 0U; index < value->type->slot_count; ++index) {
        tinypy_value_t *slot = *tinypy_internal_object_member_slot(value, index);

        if (slot != NULL) {
            visit(slot, user_data);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_fixed_builtin_subclass_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    if (value->type->layout_kind == TINYPY_VALUE_ENUMERATE) {
        tinypy_internal_enumerate_release_references(value, visit, user_data);
    }
    else if (value->type->layout_kind == TINYPY_VALUE_REVERSED) {
        tinypy_internal_reversed_release_references(value, visit, user_data);
    }
    else if (value->type->layout_kind == TINYPY_VALUE_MODULE) {
        tinypy_internal_module_release_references(value, visit, user_data);
        __tinypy_internal_fixed_builtin_subclass_slots_release_references(value, visit, user_data);
        return;
    }
    else if (value->type->layout_kind == TINYPY_VALUE_SUPER) {
        tinypy_internal_super_release_references(value, visit, user_data);
    }
    else if (value->type->layout_kind == TINYPY_VALUE_PARTIAL) {
        tinypy_internal_partial_release_references(value, visit, user_data);
        __tinypy_internal_fixed_builtin_subclass_slots_release_references(value, visit, user_data);
        return;
    }
    else if (value->type->layout_kind == TINYPY_VALUE_STATIC_METHOD || value->type->layout_kind == TINYPY_VALUE_CLASS_METHOD) {
        tinypy_internal_callable_descriptor_release_references(value, visit, user_data);
    }
    else if (value->type->layout_kind == TINYPY_VALUE_PROPERTY) {
        tinypy_internal_property_release_references(value, visit, user_data);
    }
    tinypy_internal_instance_release_references(value, visit, user_data);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_builtin_subclass_destroy(tinypy_value_t *value) {
    switch (value->type->layout_kind) {
    case TINYPY_VALUE_LIST:
        tinypy_internal_list_destroy(value);
        break;
    case TINYPY_VALUE_DICT:
        tinypy_internal_dict_destroy(value);
        break;
    case TINYPY_VALUE_BYTEARRAY:
        tinypy_internal_bytearray_destroy(value);
        break;
    default:
        break;
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_type_mro_size_raw(const tinypy_type_t *type) {
    size_t count = 0U;

    if (type->mro != NULL) {
        size_t return_value_1 = TINYPY_SIZED_SIZE(type->mro);
        return return_value_1;
    }

    const tinypy_type_t *current = type;
    while (current != NULL) {
        count += 1U;
        current = current->base_type;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_type_mro_at_raw(const tinypy_type_t *type, size_t index) {
    if (type->mro != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(type->mro);

        return (tinypy_type_t *)items[index];
    }

    const tinypy_type_t *current = type;
    while (index != 0U && current != NULL) {
        current = current->base_type;
        index -= 1U;
    }
    return (tinypy_type_t *)current;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_type_bases_size_raw(const tinypy_type_t *type) {
    if (type->bases != NULL) {
        size_t return_value_1 = TINYPY_SIZED_SIZE(type->bases);
        return return_value_1;
    }
    return type->base_type != NULL ? 1U : 0U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_type_base_at_raw(const tinypy_type_t *type, size_t index) {
    if (type->bases != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(type->bases);

        return (tinypy_type_t *)items[index];
    }
    return index == 0U ? type->base_type : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_remove_subclass(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_type_t *base = (tinypy_type_t *)user_data;
    size_t size;
    size_t found = SIZE_MAX;
    size_t index;

    (void)function;
    (void)kwargs;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *reference = TINYPY_TUPLE_GET(args, 0U);
    if (base->subclasses == NULL) {
        tinypy_value_t *return_value_1 = tinypy_none_get(base->vm);
        return return_value_1;
    }
    size = TINYPY_LIST_SIZE(base->subclasses);
    for (index = 0U; index < size; ++index) {
        if (TINYPY_LIST_GET(base->subclasses, index) == reference) {
            found = index;
            break;
        }
    }
    if (found != SIZE_MAX) {
        tinypy_list_delete(base->subclasses, found);
    }
    tinypy_value_t *return_value_2 = tinypy_none_get(base->vm);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_add_subclass(tinypy_type_t *base, tinypy_type_t *subclass) {
    tinypy_error_t *error = NULL;

    tinypy_value_t *callback = tinypy_native_function_new(base->vm, "__remove_subclass", 17U, __tinypy_internal_type_remove_subclass, base, NULL);
    tinypy_value_t *reference = tinypy_weakref_new(&subclass->base.base, callback, &error);
    TINYPY_DECREF(callback);
    if (reference == NULL) {
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_clear_raised(base->vm);
        return;
    }
    if (base->subclasses == NULL) {
        base->subclasses = tinypy_list_from_items(base->vm, NULL, 0U);
    }
    tinypy_list_append(base->subclasses, reference);
    TINYPY_DECREF(reference);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_subclasses(tinypy_type_t *type, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_list_from_items(type->vm, NULL, 0U);
    size_t index;
    size_t size;

    if (type->subclasses == NULL) {
        return result;
    }
    size = TINYPY_LIST_SIZE(type->subclasses);
    if (tinypy_internal_list_reserve_checked(type->vm, result, size, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (index = 0U; index != size; ++index) {
        tinypy_value_t *subclass = tinypy_weakref_get(TINYPY_LIST_GET(type->subclasses, index));

        if (subclass != NULL) {
            if (tinypy_internal_list_append_checked(result, subclass, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_type_is_subtype(const tinypy_type_t *type, const tinypy_type_t *candidate_base) {
    size_t count;
    size_t index;

    count = __tinypy_internal_type_mro_size_raw(type);
    for (index = 0U; index < count; ++index) {
        if (__tinypy_internal_type_mro_at_raw(type, index) == candidate_base) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_type_as_value(tinypy_type_t *type) {
    return &type->base.base;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_value_t *tinypy_type_as_const_value(const tinypy_type_t *type) {
    return &type->base.base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_value_as_type(tinypy_value_t *value) {
    return (tinypy_type_t *)value;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_value_as_const_type(const tinypy_value_t *value) {
    return (const tinypy_type_t *)value;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_type_bases_size(const tinypy_type_t *type) {
    size_t return_value_1 = __tinypy_internal_type_bases_size_raw(type);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_base_at(const tinypy_type_t *type, size_t index) {
    const tinypy_type_t *return_value_1 = __tinypy_internal_type_base_at_raw(type, index);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_type_mro_size(const tinypy_type_t *type) {
    size_t return_value_1 = __tinypy_internal_type_mro_size_raw(type);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_mro_at(const tinypy_type_t *type, size_t index) {
    const tinypy_type_t *return_value_1 = __tinypy_internal_type_mro_at_raw(type, index);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_select_metaclass(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_error_t **out_error) {
    tinypy_type_t *winner;
    size_t index;

    if (explicit_metaclass != NULL) {
        if (!tinypy_type_is_subtype(explicit_metaclass, &vm->types[TINYPY_VALUE_TYPE])) {
            __tinypy_internal_type_error(
                vm,
                "explicit metaclass is not a subtype of type", out_error);
            return NULL;
        }
        winner = (tinypy_type_t *)explicit_metaclass;
    }
    else {
        winner = bases[0]->base.base.type;
    }

    for (index = 0U; index < base_count; ++index) {
        tinypy_type_t *base_metaclass = bases[index]->base.base.type;

        if (tinypy_type_is_subtype(winner, base_metaclass)) {
            continue;
        }
        if (tinypy_type_is_subtype(base_metaclass, winner)) {
            winner = base_metaclass;
            continue;
        }
        __tinypy_internal_type_error(
            vm,
            "metaclass conflict between the selected bases", out_error);
        return NULL;
    }

    return winner;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_mro_head_in_tail(tinypy_type_t *candidate, const tinypy_mro_sequence_t *sequences, size_t sequence_count) {
    size_t sequence_index;

    for (sequence_index = 0U;
         sequence_index < sequence_count;
         ++sequence_index) {
        const tinypy_mro_sequence_t *sequence = &sequences[sequence_index];
        size_t item_index;

        for (item_index = sequence->position + 1U;
             item_index < sequence->size;
             ++item_index) {
            if (sequence->items[item_index] == candidate) {
                return TINYPY_TRUE;
            }
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_mro_free(tinypy_vm_t *vm, tinypy_mro_sequence_t *sequences, size_t sequence_size, tinypy_type_t **storage, size_t storage_size, tinypy_type_t **result, size_t result_size) {
    if (result != NULL) {
        tinypy_internal_vm_deallocate(
            vm, result, result_size);
    }
    if (storage != NULL) {
        tinypy_internal_vm_deallocate(
            vm, storage, storage_size);
    }
    if (sequences != NULL) {
        tinypy_internal_vm_deallocate(
            vm, sequences, sequence_size);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t **__tinypy_internal_c3_merge(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, size_t *out_count, size_t *out_allocation_size, tinypy_error_t **out_error) {
    tinypy_mro_sequence_t *sequences = NULL;
    tinypy_type_t **storage = NULL;
    tinypy_type_t **result = NULL;
    size_t sequence_count;
    size_t sequence_size;
    size_t total_items = base_count;
    size_t storage_size;
    size_t result_capacity;
    size_t result_size;
    size_t result_count = 0U;
    size_t storage_offset = 0U;
    size_t index;

    *out_count = 0U;
    *out_allocation_size = 0U;

    sequence_count = base_count + 1U;
    sequence_size = sequence_count * sizeof(*sequences);

    for (index = 0U; index < base_count; ++index) {
        size_t base_mro_size = __tinypy_internal_type_mro_size_raw(bases[index]);

        total_items += base_mro_size;
    }
    storage_size = total_items * sizeof(*storage);
    result_capacity = total_items + 1U;
    result_size = result_capacity * sizeof(*result);

    sequences = (tinypy_mro_sequence_t *)tinypy_internal_vm_allocate(
        vm, sequence_size);
    storage = (tinypy_type_t **)tinypy_internal_vm_allocate(
        vm, storage_size);
    result = (tinypy_type_t **)tinypy_internal_vm_allocate(
        vm, result_size);
    (void)memset(sequences, 0, sequence_size);
    result[0] = NULL;

    for (index = 0U; index < base_count; ++index) {
        tinypy_mro_sequence_t *sequence = &sequences[index];
        size_t mro_index;

        sequence->items = &storage[storage_offset];
        sequence->size = __tinypy_internal_type_mro_size_raw(bases[index]);
        for (mro_index = 0U;
             mro_index < sequence->size;
             ++mro_index) {
            sequence->items[mro_index] =
                __tinypy_internal_type_mro_at_raw(bases[index], mro_index);
        }
        storage_offset += sequence->size;
    }
    sequences[base_count].items = &storage[storage_offset];
    sequences[base_count].size = base_count;
    for (index = 0U; index < base_count; ++index) {
        sequences[base_count].items[index] = (tinypy_type_t *)bases[index];
    }

    for (;;) {
        tinypy_type_t *candidate = NULL;
        tinypy_bool_t has_items = TINYPY_FALSE;
        size_t sequence_index;

        for (sequence_index = 0U;
             sequence_index < sequence_count;
             ++sequence_index) {
            tinypy_mro_sequence_t *sequence = &sequences[sequence_index];

            if (sequence->position == sequence->size) {
                continue;
            }
            has_items = 1;
            candidate = sequence->items[sequence->position];
            if (!__tinypy_internal_mro_head_in_tail(
                    candidate, sequences, sequence_count)) {
                break;
            }
            candidate = NULL;
        }

        if (!has_items) {
            break;
        }
        if (candidate == NULL) {
            __tinypy_internal_mro_free(
                vm, sequences, sequence_size, storage, storage_size,
                result, result_size);
            __tinypy_internal_type_error(
                vm,
                "cannot create a consistent C3 method resolution order",
                out_error);
            return NULL;
        }

        result_count += 1U;
        result[result_count] = candidate;
        for (sequence_index = 0U;
             sequence_index < sequence_count;
             ++sequence_index) {
            tinypy_mro_sequence_t *sequence = &sequences[sequence_index];

            if (sequence->position < sequence->size && sequence->items[sequence->position] == candidate) {
                sequence->position += 1U;
            }
        }
    }

    tinypy_internal_vm_deallocate(
        vm, storage, storage_size);
    tinypy_internal_vm_deallocate(
        vm, sequences, sequence_size);
    *out_count = result_count;
    *out_allocation_size = result_size;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_validate_bases(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, tinypy_error_t **out_error) {
    size_t index;

    for (index = 0U; index < base_count; ++index) {
        size_t earlier;

        if ((bases[index]->flags & TINYPY_TYPE_FLAG_BASE_TYPE) == 0U) {
            __tinypy_internal_type_error(
                vm,
                "selected type does not permit subclassing", out_error);
            return TINYPY_FALSE;
        }
        for (earlier = 0U; earlier < index; ++earlier) {
            if (bases[earlier] == bases[index]) {
                __tinypy_internal_type_error(
                    vm, "duplicate type base", out_error);
                return TINYPY_FALSE;
            }
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_release_if_not_null(tinypy_value_t *value) {
    if (value != NULL) {
        TINYPY_DECREF(value);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_namespace_value(tinypy_vm_t *vm, tinypy_value_t *namespace_dict, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_dict_get_optional(namespace_dict, key);

    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_type_slot_name_equal(tinypy_value_t *value, const char *name, size_t name_size) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);

    tinypy_bool_t return_value_1 = size == name_size && (size == 0U || memcmp(bytes, name, size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_mangle_slot_name(tinypy_vm_t *vm, const char *class_name, size_t class_name_size, tinypy_value_t *slot_name) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(slot_name);
    size_t size = TINYPY_TEXT_BYTE_SIZE(slot_name);
    size_t class_start = 0U;
    uint8_t *mangled;
    size_t mangled_size;

    if (size < 3U || bytes[0] != '_' || bytes[1] != '_' || (bytes[size - 2U] == '_' && bytes[size - 1U] == '_')) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, bytes, size);
        return return_value_1;
    }
    while (class_start < class_name_size && class_name[class_start] == '_') {
        class_start += 1U;
    }
    if (class_start == class_name_size) {
        tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, bytes, size);
        return return_value_2;
    }
    mangled_size = 1U + class_name_size - class_start + size;
    mangled = (uint8_t *)tinypy_internal_vm_allocate(vm, mangled_size);
    mangled[0] = '_';
    (void)memcpy(mangled + 1U, class_name + class_start, class_name_size - class_start);
    (void)memcpy(mangled + 1U + class_name_size - class_start, bytes, size);
    tinypy_value_t *result = tinypy_string_from_bytes(vm, mangled, mangled_size);
    tinypy_internal_vm_deallocate(vm, mangled, mangled_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_parse_slots(tinypy_vm_t *vm, const char *class_name, size_t class_name_size, tinypy_value_t *namespace_dict, int32_t *out_declared, int32_t *out_dict, int32_t *out_weakref, tinypy_error_t **out_error) {
    tinypy_value_t *declaration = __tinypy_internal_type_namespace_value(vm, namespace_dict, "__slots__", 9U);
    tinypy_value_t *names = tinypy_list_from_items(vm, NULL, 0U);
    size_t input_size = 0U;
    size_t index;

    *out_declared = declaration != NULL ? INT32_C(1) : INT32_C(0);
    *out_dict = INT32_C(0);
    *out_weakref = INT32_C(0);
    if (declaration == NULL) {
        TINYPY_DECREF(names);
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_UNICODE) {
        input_size = 1U;
    }
    else if (TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_TUPLE) {
        input_size = TINYPY_TUPLE_SIZE(declaration);
    }
    else if (TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_LIST) {
        input_size = TINYPY_LIST_SIZE(declaration);
    }
    else {
        TINYPY_DECREF(names);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__slots__ must be a string or a sequence of strings", out_error);
        return NULL;
    }
    for (index = 0U; index < input_size; ++index) {
        tinypy_value_t *source = input_size == 1U && (TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_UNICODE)
                                     ? declaration
                                     : (TINYPY_VALUE_KIND(declaration) == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(declaration, index) : TINYPY_LIST_GET(declaration, index));
        tinypy_value_t *name;

        if (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE) {
            TINYPY_DECREF(names);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__slots__ entries must be strings", out_error);
            return NULL;
        }
        if (__tinypy_internal_type_slot_name_equal(source, "__dict__", 8U) != 0) {
            if (*out_dict != 0) {
                TINYPY_DECREF(names);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__dict__ slot is duplicated", out_error);
                return NULL;
            }
            *out_dict = INT32_C(1);
            continue;
        }
        if (__tinypy_internal_type_slot_name_equal(source, "__weakref__", 11U) != 0) {
            if (*out_weakref != 0) {
                TINYPY_DECREF(names);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__weakref__ slot is duplicated", out_error);
                return NULL;
            }
            *out_weakref = INT32_C(1);
            continue;
        }
        name = __tinypy_internal_type_mangle_slot_name(vm, class_name, class_name_size, source);
        tinypy_value_t *const *iterator = TINYPY_LIST_ITERATOR_BEGIN(names);
        tinypy_value_t *const *iterator_end = TINYPY_LIST_ITERATOR_END(names);
        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            if (tinypy_internal_equal_value(item, name, 1) != 0) {
                TINYPY_DECREF(name);
                TINYPY_DECREF(names);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__slots__ entry is duplicated", out_error);
                return NULL;
            }
        }
        if (tinypy_internal_list_append_checked(names, name, out_error) == 0) {
            TINYPY_DECREF(name);
            TINYPY_DECREF(names);
            return NULL;
        }
        TINYPY_DECREF(name);
    }
    size_t list_size = TINYPY_LIST_SIZE(names);
    tinypy_value_t **items = list_size != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, list_size * sizeof(*items)) : NULL;
    for (index = 0U; index < list_size; ++index) {
        items[index] = TINYPY_LIST_GET(names, index);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, list_size);
    if (items != NULL) {
        tinypy_internal_vm_deallocate(vm, items, list_size * sizeof(*items));
    }
    TINYPY_DECREF(names);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_type_t *__tinypy_internal_select_layout_base(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, tinypy_error_t **out_error) {
    const tinypy_type_t *layout_base = bases[0];
    const tinypy_type_t *native_base = NULL;
    tinypy_value_type_e builtin_layout_kind = TINYPY_VALUE_INVALID;
    size_t index;

    for (index = 0U; index < base_count; ++index) {
        const tinypy_type_t *candidate = bases[index];

        if (candidate->layout_kind != TINYPY_VALUE_NATIVE_INSTANCE) {
            continue;
        }
        if (native_base == NULL) {
            native_base = candidate;
            continue;
        }
        if (candidate->native_payload_offset != native_base->native_payload_offset || candidate->native_payload_size != native_base->native_payload_size || candidate->native_payload_alignment != native_base->native_payload_alignment) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "multiple bases have incompatible native instance layouts", out_error);
            return NULL;
        }
    }
    if (native_base != NULL) {
        return native_base;
    }
    for (index = 0U; index < base_count; ++index) {
        const tinypy_type_t *candidate = bases[index];
        tinypy_value_type_e candidate_kind = candidate->layout_kind;

        if (candidate_kind == TINYPY_VALUE_INVALID || candidate_kind == TINYPY_VALUE_INSTANCE) {
            continue;
        }
        if (builtin_layout_kind == TINYPY_VALUE_INVALID) {
            builtin_layout_kind = candidate_kind;
            layout_base = candidate;
            continue;
        }
        if (candidate_kind != builtin_layout_kind) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "multiple bases have incompatible instance layouts", out_error);
            return NULL;
        }
    }
    for (index = 0U; index < base_count; ++index) {
        const tinypy_type_t *candidate = bases[index];

        if (candidate->slot_count == 0U || candidate == layout_base) {
            continue;
        }
        if (builtin_layout_kind != TINYPY_VALUE_INVALID && candidate->layout_kind == TINYPY_VALUE_INSTANCE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "multiple bases have incompatible instance layouts", out_error);
            return NULL;
        }
        if (layout_base->slot_count == 0U) {
            layout_base = candidate;
            continue;
        }
        if (tinypy_type_is_subtype(candidate, layout_base) != 0) {
            layout_base = candidate;
            continue;
        }
        if (tinypy_type_is_subtype(layout_base, candidate) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "multiple bases have incompatible instance layouts", out_error);
            return NULL;
        }
    }
    return layout_base;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_rebase_record_t {
    tinypy_type_t *type;
    tinypy_value_t *old_mro;
    tinypy_value_t *new_mro;
    tinypy_bool_t processed;
} tinypy_rebase_record_t;
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_rebase_find(const tinypy_rebase_record_t *records, size_t count, const tinypy_type_t *type) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (records[index].type == type) {
            return index;
        }
    }
    return SIZE_MAX;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_rebase_release_records(tinypy_vm_t *vm, tinypy_rebase_record_t *records, size_t count, size_t capacity);
//////////////////////////////////////////////////////////////////////////
static tinypy_rebase_record_t *__tinypy_internal_rebase_collect(tinypy_type_t *type, size_t *out_count, size_t *out_capacity, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t capacity = 8U;
    size_t count = 1U;
    size_t scan = 0U;
    tinypy_rebase_record_t *records;

    *out_count = 0U;
    *out_capacity = 0U;
    records = (tinypy_rebase_record_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*records), out_error);
    if (records == NULL) {
        return NULL;
    }
    (void)memset(records, 0, capacity * sizeof(*records));
    records[0].type = type;
    records[0].old_mro = type->mro;
    TINYPY_INCREF(&type->base.base);
    while (scan < count) {
        tinypy_type_t *current = records[scan++].type;
        size_t subclass_count = current->subclasses != NULL ? TINYPY_LIST_SIZE(current->subclasses) : 0U;
        size_t index;

        for (index = 0U; index < subclass_count; ++index) {
            tinypy_value_t *subclass_value = tinypy_weakref_get(TINYPY_LIST_GET(current->subclasses, index));
            tinypy_type_t *subclass;

            if (subclass_value == NULL) {
                continue;
            }
            subclass = (tinypy_type_t *)subclass_value;
            if (__tinypy_internal_rebase_find(records, count, subclass) != SIZE_MAX) {
                continue;
            }
            if (count == capacity) {
                size_t new_capacity;
                tinypy_rebase_record_t *grown;

                if (capacity > SIZE_MAX / 2U || capacity * 2U > SIZE_MAX / sizeof(*records)) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "type descendant list is too large", out_error);
                    __tinypy_internal_rebase_release_records(vm, records, count, capacity);
                    return NULL;
                }
                new_capacity = capacity * 2U;
                grown = (tinypy_rebase_record_t *)tinypy_internal_vm_reallocate_checked(vm, records, capacity * sizeof(*records), new_capacity * sizeof(*records), out_error);
                if (grown == NULL) {
                    __tinypy_internal_rebase_release_records(vm, records, count, capacity);
                    return NULL;
                }
                (void)memset(grown + count, 0, (new_capacity - count) * sizeof(*grown));
                records = grown;
                capacity = new_capacity;
            }
            records[count].type = subclass;
            records[count].old_mro = subclass->mro;
            TINYPY_INCREF(subclass_value);
            count += 1U;
        }
    }
    *out_count = count;
    *out_capacity = capacity;
    return records;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_rebase_release_records(tinypy_vm_t *vm, tinypy_rebase_record_t *records, size_t count, size_t capacity) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        TINYPY_DECREF(&records[index].type->base.base);
    }
    tinypy_internal_vm_deallocate(vm, records, capacity * sizeof(*records));
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_rebase_destroy_mro(tinypy_vm_t *vm, tinypy_value_t *mro) {
    tinypy_internal_value_destroy(mro);
    TINYPY_DECREF(&vm->types[TINYPY_VALUE_TUPLE].base.base);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_rebase_mro(tinypy_type_t *type, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t base_count = __tinypy_internal_type_bases_size_raw(type);
    const tinypy_type_t **bases = (const tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*bases));
    tinypy_type_t **merged;
    tinypy_value_t **values;
    tinypy_value_t *mro;
    size_t merged_count;
    size_t merged_size;
    size_t index;

    for (index = 0U; index < base_count; ++index) {
        bases[index] = __tinypy_internal_type_base_at_raw(type, index);
    }
    merged = __tinypy_internal_c3_merge(vm, bases, base_count, &merged_count, &merged_size, out_error);
    tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
    if (merged == NULL) {
        return NULL;
    }
    values = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (merged_count + 1U) * sizeof(*values));
    values[0] = &type->base.base;
    for (index = 0U; index < merged_count; ++index) {
        values[index + 1U] = &merged[index + 1U]->base.base;
    }
    mro = tinypy_internal_tuple_from_borrowed_items(vm, values, merged_count + 1U);
    tinypy_internal_vm_deallocate(vm, values, (merged_count + 1U) * sizeof(*values));
    tinypy_internal_vm_deallocate(vm, merged, merged_size);
    return mro;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_rebase_layout_compatible(const tinypy_type_t *old_base, const tinypy_type_t *new_base) {
    if (old_base == new_base) {
        return TINYPY_TRUE;
    }
    if (old_base->layout_kind == TINYPY_VALUE_NATIVE_INSTANCE || new_base->layout_kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        return TINYPY_FALSE;
    }
    return old_base->layout_kind == new_base->layout_kind
        && old_base->basic_size == new_base->basic_size
        && old_base->item_size == new_base->item_size
        && old_base->slots_offset == new_base->slots_offset
        && old_base->slot_count == new_base->slot_count
        && old_base->has_instance_dict == new_base->has_instance_dict
        && old_base->dict_offset == new_base->dict_offset
        && old_base->weakref_offset == new_base->weakref_offset
        && old_base->number_slots == new_base->number_slots
        && old_base->sequence_slots == new_base->sequence_slots
        && old_base->mapping_slots == new_base->mapping_slots
        && old_base->repr == new_base->repr
        && old_base->string == new_base->string
        && old_base->hash == new_base->hash
        && old_base->call == new_base->call
        && old_base->get_attribute == new_base->get_attribute
        && old_base->set_attribute == new_base->set_attribute
        && old_base->rich_compare == new_base->rich_compare
        && old_base->release_references == new_base->release_references
        && old_base->traverse_references == new_base->traverse_references
        && old_base->destroy == new_base->destroy
        && old_base->iter == new_base->iter
        && old_base->next == new_base->next
        && old_base->descriptor_get == new_base->descriptor_get
        && old_base->descriptor_set == new_base->descriptor_set
        && old_base->initialize == new_base->initialize
        && old_base->create == new_base->create
            ? TINYPY_TRUE
            : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_rebase_has_type(tinypy_value_t *bases, const tinypy_type_t *type) {
    size_t index;

    for (index = 0U; index < TINYPY_TUPLE_SIZE(bases); ++index) {
        if (TINYPY_TUPLE_GET(bases, index) == &type->base.base) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_rebase_remove_subclass(tinypy_type_t *base, const tinypy_type_t *subclass) {
    size_t size = base->subclasses != NULL ? TINYPY_LIST_SIZE(base->subclasses) : 0U;
    size_t found = SIZE_MAX;
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (tinypy_weakref_get(TINYPY_LIST_GET(base->subclasses, index)) == &subclass->base.base) {
            found = index;
            break;
        }
    }
    if (found != SIZE_MAX) {
        tinypy_list_delete(base->subclasses, found);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_type_set_bases(tinypy_type_t *type, tinypy_value_t *bases_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    const tinypy_type_t **bases = NULL;
    const tinypy_type_t *layout_base;
    tinypy_type_t *metaclass;
    tinypy_value_t *old_bases;
    tinypy_rebase_record_t *records;
    size_t record_count;
    size_t record_capacity;
    size_t base_count;
    size_t processed_count = 0U;
    size_t index;

    TINYPY_CLEAR_ERROR(out_error);
    if ((type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type attributes are read-only", out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(bases_value) != TINYPY_VALUE_TUPLE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type __bases__ must be a tuple", out_error);
        return TINYPY_FALSE;
    }
    base_count = TINYPY_TUPLE_SIZE(bases_value);
    if (base_count == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type __bases__ must not be empty", out_error);
        return TINYPY_FALSE;
    }
    if (base_count > SIZE_MAX / sizeof(*bases)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "type base list is too large", out_error);
        return TINYPY_FALSE;
    }
    bases = (const tinypy_type_t **)tinypy_internal_vm_allocate_checked(vm, base_count * sizeof(*bases), out_error);
    if (bases == NULL) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < base_count; ++index) {
        tinypy_value_t *base_value = TINYPY_TUPLE_GET(bases_value, index);

        if (TINYPY_VALUE_KIND(base_value) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type __bases__ entries must be types", out_error);
            tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
            return TINYPY_FALSE;
        }
        bases[index] = (tinypy_type_t *)base_value;
        if (bases[index] == type || tinypy_type_is_subtype(bases[index], type) != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a __bases__ item causes an inheritance cycle", out_error);
            tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
            return TINYPY_FALSE;
        }
    }
    if (__tinypy_internal_validate_bases(vm, bases, base_count, out_error) == 0) {
        tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
        return TINYPY_FALSE;
    }
    layout_base = __tinypy_internal_select_layout_base(vm, bases, base_count, out_error);
    if (layout_base == NULL || __tinypy_internal_rebase_layout_compatible(type->base_type, layout_base) == 0) {
        if (layout_base != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__bases__ assignment changes the instance layout", out_error);
        }
        tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
        return TINYPY_FALSE;
    }
    metaclass = __tinypy_internal_select_metaclass(vm, bases, base_count, type->base.base.type, out_error);
    if (metaclass == NULL || metaclass != type->base.base.type) {
        if (metaclass != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__bases__ assignment would change the metaclass", out_error);
        }
        tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
        return TINYPY_FALSE;
    }

    records = __tinypy_internal_rebase_collect(type, &record_count, &record_capacity, out_error);
    if (records == NULL) {
        tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
        return TINYPY_FALSE;
    }
    old_bases = type->bases;
    TINYPY_INCREF(bases_value);
    type->bases = bases_value;
    while (processed_count < record_count) {
        tinypy_bool_t progress = TINYPY_FALSE;

        for (index = 0U; index < record_count; ++index) {
            tinypy_rebase_record_t *record = &records[index];
            size_t candidate_base_count;
            size_t base_index;
            tinypy_bool_t ready = TINYPY_TRUE;

            if (record->processed != 0) {
                continue;
            }
            candidate_base_count = __tinypy_internal_type_bases_size_raw(record->type);
            for (base_index = 0U; base_index < candidate_base_count; ++base_index) {
                tinypy_type_t *candidate_base = __tinypy_internal_type_base_at_raw(record->type, base_index);
                size_t record_index = __tinypy_internal_rebase_find(records, record_count, candidate_base);

                if (record_index != SIZE_MAX && records[record_index].processed == 0) {
                    ready = TINYPY_FALSE;
                    break;
                }
            }
            if (ready == 0) {
                continue;
            }
            record->new_mro = __tinypy_internal_rebase_mro(record->type, out_error);
            if (record->new_mro == NULL) {
                goto rollback;
            }
            record->type->mro = record->new_mro;
            record->processed = TINYPY_TRUE;
            processed_count += 1U;
            progress = TINYPY_TRUE;
        }
        if (progress == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot update descendant method resolution order", out_error);
            goto rollback;
        }
    }

    for (index = 0U; index < TINYPY_TUPLE_SIZE(old_bases); ++index) {
        tinypy_type_t *old_base = (tinypy_type_t *)TINYPY_TUPLE_GET(old_bases, index);

        if (__tinypy_internal_rebase_has_type(bases_value, old_base) == 0) {
            __tinypy_internal_rebase_remove_subclass(old_base, type);
        }
    }
    for (index = 0U; index < base_count; ++index) {
        if (__tinypy_internal_rebase_has_type(old_bases, bases[index]) == 0) {
            __tinypy_internal_type_add_subclass((tinypy_type_t *)bases[index], type);
        }
    }
    type->base_type = (tinypy_type_t *)layout_base;
    tinypy_internal_type_lookup_cache_invalidate(vm);
    for (index = 0U; index < record_count; ++index) {
        tinypy_rebase_record_t *record = &records[index];

        __tinypy_internal_rebase_destroy_mro(vm, record->old_mro);
        record->type->version_tag += UINT64_C(1);
        if (record->type->version_tag == 0U) {
            record->type->version_tag = UINT64_C(1);
        }
        record->type->has_finalizer = tinypy_type_get_attr(record->type, "__del__", 7U) != NULL ? INT32_C(1) : INT32_C(0);
    }
    TINYPY_DECREF(old_bases);
    __tinypy_internal_rebase_release_records(vm, records, record_count, record_capacity);
    tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
    return TINYPY_TRUE;

rollback:
    type->bases = old_bases;
    TINYPY_DECREF(bases_value);
    for (index = 0U; index < record_count; ++index) {
        tinypy_rebase_record_t *record = &records[index];

        if (record->new_mro != NULL) {
            record->type->mro = record->old_mro;
            __tinypy_internal_rebase_destroy_mro(vm, record->new_mro);
        }
    }
    __tinypy_internal_rebase_release_records(vm, records, record_count, record_capacity);
    tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, int32_t configured_instance_dict, int32_t configured_weakrefs, tinypy_error_t **out_error) {
    const tinypy_type_t *default_base = NULL;
    const tinypy_type_t *const *actual_bases = bases;
    size_t actual_base_count = base_count;
    tinypy_type_t *metaclass = NULL;
    tinypy_type_t **mro_types = NULL;
    size_t mro_tail_count = 0U;
    size_t mro_workspace_size = 0U;
    tinypy_type_t *type = NULL;
    tinypy_value_t *name_object = NULL;
    tinypy_value_t *dict = NULL;
    tinypy_value_t *bases_tuple = NULL;
    tinypy_value_t *mro_tuple = NULL;
    tinypy_value_t *own_slots = NULL;
    tinypy_value_t **base_values = NULL;
    tinypy_value_t **mro_values = NULL;
    size_t base_values_size = 0U;
    size_t mro_values_size = 0U;
    tinypy_value_type_e instance_kind;
    int32_t slots_declared = INT32_C(0);
    int32_t dict_slot = INT32_C(0);
    int32_t weakref_slot = INT32_C(0);
    size_t inherited_slot_count = 0U;
    size_t index;

    TINYPY_CLEAR_ERROR(out_error);

    if (name_size != 0U && memchr(name, '\0', name_size) != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "type name must not contain null characters", out_error);
        return NULL;
    }

    if (actual_base_count == 0U) {
        default_base = &vm->types[TINYPY_VALUE_INSTANCE];
        actual_bases = &default_base;
        actual_base_count = 1U;
    }
    if (__tinypy_internal_validate_bases(
            vm, actual_bases, actual_base_count, out_error) == 0) {
        return NULL;
    }
    const tinypy_type_t *layout_base = __tinypy_internal_select_layout_base(vm, actual_bases, actual_base_count, out_error);
    if (layout_base == NULL) {
        return NULL;
    }
    metaclass = __tinypy_internal_select_metaclass(
        vm, actual_bases, actual_base_count,
        explicit_metaclass, out_error);
    if (metaclass == NULL) {
        return NULL;
    }
    mro_types = __tinypy_internal_c3_merge(
        vm, actual_bases, actual_base_count,
        &mro_tail_count, &mro_workspace_size, out_error);
    if (mro_types == NULL) {
        return NULL;
    }
    if (namespace_dict == NULL) {
        dict = tinypy_dict_new(vm);
    }
    else {
        TINYPY_INCREF(namespace_dict);
        dict = namespace_dict;
    }
    own_slots = __tinypy_internal_type_parse_slots(vm, name, name_size, dict, &slots_declared, &dict_slot, &weakref_slot, out_error);
    if (own_slots == NULL) {
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size);
        return NULL;
    }
    if (layout_base->layout_kind == TINYPY_VALUE_TUPLE && TINYPY_TUPLE_SIZE(own_slots) != 0U) {
        TINYPY_DECREF(own_slots);
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "nonempty __slots__ are not supported for tuple subtypes", out_error);
        return NULL;
    }
    instance_kind = tinypy_type_is_subtype(layout_base, &vm->types[TINYPY_VALUE_TYPE]) != 0
                        ? TINYPY_VALUE_TYPE
                        : (layout_base->layout_kind != TINYPY_VALUE_INVALID ? layout_base->layout_kind : TINYPY_VALUE_INSTANCE);
    type = (tinypy_type_t *)tinypy_internal_object_allocate(
        vm, metaclass, sizeof(*type));

    type->vm = vm;
    inherited_slot_count = layout_base->slot_count;
    type->slot_count = inherited_slot_count + TINYPY_TUPLE_SIZE(own_slots);
    tinypy_bool_t add_instance_dict = configured_instance_dict >= 0 ? (configured_instance_dict != 0 ? TINYPY_TRUE : TINYPY_FALSE) : (slots_declared == 0 || dict_slot != 0 ? TINYPY_TRUE : TINYPY_FALSE);
    tinypy_bool_t add_weakrefs = configured_weakrefs >= 0 ? (configured_weakrefs != 0 ? TINYPY_TRUE : TINYPY_FALSE) : (slots_declared == 0 || weakref_slot != 0 ? TINYPY_TRUE : TINYPY_FALSE);

    type->has_instance_dict = layout_base->has_instance_dict != 0 || add_instance_dict != 0 ? INT32_C(1) : INT32_C(0);
    type->layout_kind = instance_kind;
    if (instance_kind == TINYPY_VALUE_TYPE) {
        type->basic_size = sizeof(tinypy_type_t);
        type->dict_offset = 0U;
        type->slots_offset = 0U;
        type->weakref_offset = offsetof(tinypy_type_t, weakrefs);
    }
    else if (instance_kind == TINYPY_VALUE_WEAKREF) {
        type->slots_offset = offsetof(tinypy_weakref_object_t, slots);
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_weakref_object_t, dict) : 0U;
    }
    else if (instance_kind == TINYPY_VALUE_TUPLE) {
        type->slots_offset = 0U;
        type->slot_count = 0U;
        type->basic_size = sizeof(tinypy_tuple_subclass_object_t);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_tuple_subclass_object_t, dict) : 0U;
    }
    else if (instance_kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        type->native_payload_offset = layout_base->native_payload_offset;
        type->native_payload_size = layout_base->native_payload_size;
        type->native_payload_alignment = layout_base->native_payload_alignment;
        type->native_spec = layout_base->native_spec;
        type->native_number_slots = layout_base->native_number_slots;
        type->native_sequence_slots = layout_base->native_sequence_slots;
        type->native_mapping_slots = layout_base->native_mapping_slots;
        type->slots_offset = layout_base->slots_offset != 0U ? layout_base->slots_offset : layout_base->basic_size;
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        type->dict_offset = layout_base->dict_offset != 0U
                                ? layout_base->dict_offset
                                : (type->has_instance_dict != 0 ? offsetof(tinypy_native_instance_object_t, dict) : 0U);
        type->weakref_offset = layout_base->weakref_offset != 0U || add_weakrefs != 0
                                   ? offsetof(tinypy_native_instance_object_t, weakrefs)
                                   : 0U;
    }
    else if (__tinypy_internal_type_has_container_builtin_layout(instance_kind) != 0 || __tinypy_internal_type_has_fixed_builtin_layout(vm, instance_kind) != 0) {
        const tinypy_type_t *builtin_layout = &vm->types[instance_kind];

        type->slots_offset = builtin_layout->basic_size;
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        if (instance_kind == TINYPY_VALUE_MODULE || instance_kind == TINYPY_VALUE_PARTIAL) {
            type->has_instance_dict = INT32_C(1);
            type->dict_offset = builtin_layout->dict_offset;
        }
        else if (type->has_instance_dict != 0) {
            type->dict_offset = type->basic_size;
            type->basic_size += sizeof(tinypy_value_t *);
        }
        if (instance_kind == TINYPY_VALUE_PARTIAL) {
            type->weakref_offset = builtin_layout->weakref_offset;
        }
        else if (layout_base->weakref_offset != 0U || add_weakrefs != 0) {
            type->weakref_offset = type->basic_size;
            type->basic_size += sizeof(tinypy_value_t *);
        }
    }
    else if (__tinypy_internal_type_has_variable_immutable_builtin_layout(instance_kind) != 0) {
        const tinypy_type_t *builtin_layout = &vm->types[instance_kind];

        type->slots_offset = builtin_layout->basic_size;
        type->basic_size = builtin_layout->basic_size + type->slot_count * sizeof(tinypy_value_t *);
        if (type->has_instance_dict != 0) {
            type->dict_offset = 1U;
            type->basic_size += sizeof(tinypy_value_t *);
        }
        if (layout_base->weakref_offset != 0U || add_weakrefs != 0) {
            type->weakref_offset = 1U;
            type->basic_size += sizeof(tinypy_value_t *);
        }
    }
    else {
        type->slots_offset = offsetof(tinypy_instance_object_t, slots);
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_instance_object_t, dict) : 0U;
        if (layout_base->weakref_offset != 0U || add_weakrefs != 0) {
            type->weakref_offset = type->basic_size;
            type->basic_size += sizeof(tinypy_value_t *);
        }
    }
    type->flags = TINYPY_TYPE_FLAG_HEAP | TINYPY_TYPE_FLAG_BASE_TYPE;
    if (instance_kind == TINYPY_VALUE_TYPE) {
        type->flags |= TINYPY_TYPE_FLAG_TYPE_SUBCLASS;
    }
    type->base_type = (tinypy_type_t *)layout_base;
    type->number_slots = layout_base->number_slots == &layout_base->native_number_slots ? &type->native_number_slots : layout_base->number_slots;
    type->sequence_slots = layout_base->sequence_slots == &layout_base->native_sequence_slots ? &type->native_sequence_slots : layout_base->sequence_slots;
    type->mapping_slots = layout_base->mapping_slots == &layout_base->native_mapping_slots ? &type->native_mapping_slots : layout_base->mapping_slots;
    type->repr = layout_base->repr;
    type->string = layout_base->string;
    type->hash = layout_base->hash;
    type->call = layout_base->call;
    type->get_attribute = layout_base->get_attribute;
    type->set_attribute = layout_base->set_attribute;
    type->rich_compare = layout_base->rich_compare;
    type->iter = layout_base->iter;
    type->next = layout_base->next;
    type->descriptor_get = layout_base->descriptor_get;
    type->descriptor_set = layout_base->descriptor_set;
    type->release_references = instance_kind == TINYPY_VALUE_TYPE
                                   ? tinypy_internal_type_release_references
                                   : (instance_kind == TINYPY_VALUE_WEAKREF ? tinypy_internal_weakref_release_references : (instance_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_subclass_release_references : (instance_kind == TINYPY_VALUE_NATIVE_INSTANCE ? tinypy_internal_native_instance_release_references : (__tinypy_internal_type_has_container_builtin_layout(instance_kind) != 0 ? __tinypy_internal_builtin_subclass_release_references : (__tinypy_internal_type_has_fixed_builtin_layout(vm, instance_kind) != 0 ? __tinypy_internal_fixed_builtin_subclass_release_references : tinypy_internal_instance_release_references)))));
    type->traverse_references = type->release_references;
    type->destroy = instance_kind == TINYPY_VALUE_TYPE
                        ? tinypy_internal_type_destroy
                        : (instance_kind == TINYPY_VALUE_WEAKREF ? tinypy_internal_weakref_destroy : (instance_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_subclass_destroy : (instance_kind == TINYPY_VALUE_NATIVE_INSTANCE ? tinypy_internal_native_instance_destroy : (__tinypy_internal_type_has_container_builtin_layout(instance_kind) != 0 ? __tinypy_internal_builtin_subclass_destroy : (instance_kind == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_destroy : NULL)))));
    if (instance_kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        type->release_references = layout_base->release_references;
        type->traverse_references = layout_base->traverse_references;
        type->destroy = layout_base->destroy;
    }

    name_object = tinypy_string_from_bytes(vm, name, name_size);
    type->name = (const char *)TINYPY_STRING_OBJECT(name_object)->bytes;
    type->name_size = name_size;

    base_values_size = actual_base_count * sizeof(*base_values);
    base_values = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, base_values_size);
    for (index = 0U; index < actual_base_count; ++index) {
        base_values[index] = &((tinypy_type_t *)actual_bases[index])->base.base;
    }
    bases_tuple = tinypy_tuple_from_items(
        vm, base_values, actual_base_count);

    mro_values_size = (mro_tail_count + 1U) * sizeof(*mro_values);
    mro_values = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, mro_values_size);
    mro_values[0] = &type->base.base;
    for (index = 0U; index < mro_tail_count; ++index) {
        mro_values[index + 1U] = &mro_types[index + 1U]->base.base;
    }
    mro_tuple = tinypy_internal_tuple_from_borrowed_items(
        vm, mro_values, mro_tail_count + 1U);

    type->name_object = name_object;
    type->dict = dict;
    TINYPY_DICT_OBJECT(type->dict)->type_dictionary = INT32_C(1);
    TINYPY_DICT_OBJECT(type->dict)->type_owner = type;
    type->bases = bases_tuple;
    type->mro = mro_tuple;
    name_object = NULL;
    dict = NULL;
    bases_tuple = NULL;
    mro_tuple = NULL;

    tinypy_value_t *const *own_slots_begin = TINYPY_TUPLE_ITERATOR_BEGIN(own_slots);
    tinypy_value_t *const *own_slots_iterator = own_slots_begin;
    tinypy_value_t *const *own_slots_end = TINYPY_TUPLE_ITERATOR_END(own_slots);
    for (; own_slots_iterator != own_slots_end; ++own_slots_iterator) {
        size_t slot_index = (size_t)(own_slots_iterator - own_slots_begin);
        tinypy_value_t *slot_name = *own_slots_iterator;
        tinypy_value_t *descriptor;

        const uint8_t *bytes = TINYPY_TEXT_BYTES(slot_name);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(slot_name);
        if (__tinypy_internal_type_namespace_value(vm, type->dict, (const char *)bytes, byte_size) != NULL) {
            TINYPY_DECREF(own_slots);
            TINYPY_DECREF(&type->base.base);
            tinypy_internal_vm_deallocate(vm, mro_values, mro_values_size);
            tinypy_internal_vm_deallocate(vm, base_values, base_values_size);
            tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slot name conflicts with a class variable", out_error);
            return NULL;
        }
        descriptor = tinypy_internal_member_descriptor_new(type, slot_name, inherited_slot_count + slot_index);
        tinypy_dict_set(type->dict, slot_name, descriptor);
        TINYPY_DECREF(descriptor);
    }
    if (type->has_instance_dict != 0 && layout_base->has_instance_dict == 0) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "__dict__", 8U);
        tinypy_value_t *descriptor = tinypy_internal_instance_dict_descriptor_new(type);

        tinypy_dict_set(type->dict, key, descriptor);
        TINYPY_DECREF(descriptor);
        TINYPY_DECREF(key);
    }
    if (type->weakref_offset != 0U && layout_base->weakref_offset == 0U) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "__weakref__", 11U);
        tinypy_value_t *descriptor = tinypy_internal_instance_weakref_descriptor_new(type);

        tinypy_dict_set(type->dict, key, descriptor);
        TINYPY_DECREF(descriptor);
        TINYPY_DECREF(key);
    }
    TINYPY_DECREF(own_slots);
    type->has_finalizer = tinypy_type_get_attr(type, "__del__", 7U) != NULL ? INT32_C(1) : INT32_C(0);

    tinypy_internal_vm_deallocate(
        vm, mro_values, mro_values_size);
    tinypy_internal_vm_deallocate(
        vm, base_values, base_values_size);
    tinypy_internal_vm_deallocate(
        vm, mro_types, mro_workspace_size);
    for (index = 0U; index < actual_base_count; ++index) {
        __tinypy_internal_type_add_subclass((tinypy_type_t *)actual_bases[index], type);
    }
    return type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_type_t *result = __tinypy_internal_type_new(vm, name, name_size, bases, base_count, explicit_metaclass, namespace_dict, -1, -1, out_error);

    if (result != NULL) {
        result->flags |= TINYPY_TYPE_FLAG_PYTHON_HEAP;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_internal_type_new_configured(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_bool_t has_instance_dict, tinypy_bool_t has_weakrefs, tinypy_error_t **out_error) {
    tinypy_type_t *result = __tinypy_internal_type_new(vm, name, name_size, bases, base_count, explicit_metaclass, namespace_dict, has_instance_dict != 0 ? 1 : 0, has_weakrefs != 0 ? 1 : 0, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_lookup_cache_invalidate(tinypy_vm_t *vm) {
    vm->type_lookup_cache_epoch += UINT64_C(1);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_modified_epoch(tinypy_type_t *type, uint64_t epoch) {
    size_t index;
    size_t count;

    if (type->modification_epoch == epoch) {
        return;
    }
    type->modification_epoch = epoch;
    type->version_tag += UINT64_C(1);
    if (type->version_tag == 0U) {
        type->version_tag = UINT64_C(1);
    }
    count = type->subclasses != NULL ? TINYPY_LIST_SIZE(type->subclasses) : 0U;
    for (index = 0U; index != count; ++index) {
        tinypy_value_t *subclass = tinypy_weakref_get(TINYPY_LIST_GET(type->subclasses, index));

        if (subclass != NULL) {
            __tinypy_internal_type_modified_epoch((tinypy_type_t *)subclass, epoch);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_modified(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;

    vm->type_lookup_cache_epoch += UINT64_C(1);
    if (vm->type_lookup_cache_epoch == 0U) {
        vm->type_lookup_cache_epoch = UINT64_C(1);
    }
    __tinypy_internal_type_modified_epoch(type, vm->type_lookup_cache_epoch);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_lookup_cache_finalize(tinypy_vm_t *vm) {
    size_t index;

    for (index = 0U; index < TINYPY_TYPE_LOOKUP_CACHE_SIZE; ++index) {
        tinypy_type_lookup_cache_entry_t *entry = &vm->type_lookup_cache[index];

        if (entry->key != NULL) {
            TINYPY_DECREF(entry->key);
        }
    }
    (void)memset(vm->type_lookup_cache, 0, sizeof(vm->type_lookup_cache));
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_internal_type_lookup_cacheable(const tinypy_vm_t *vm, const tinypy_value_t *key) {
    return key->type == &vm->types[TINYPY_VALUE_STRING] ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_type_lookup_cache_index(const tinypy_type_t *type, tinypy_hash_t hash) {
    uint64_t mixed = (uint64_t)hash ^ ((uint64_t)(uintptr_t)type >> 4U);

    mixed ^= mixed >> 32U;
    mixed ^= mixed >> 16U;
    return (size_t)mixed & (TINYPY_TYPE_LOOKUP_CACHE_SIZE - 1U);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_lookup_key(tinypy_vm_t *vm, const tinypy_type_t *type, tinypy_value_t *key) {
    tinypy_type_lookup_cache_entry_t *entry = NULL;
    tinypy_hash_t hash = 0;
    size_t mro_size = __tinypy_internal_type_mro_size_raw(type);
    size_t index;

    if (__tinypy_internal_type_lookup_cacheable(vm, key) != 0) {
        hash = tinypy_internal_hash_value(key, NULL);
        entry = &vm->type_lookup_cache[__tinypy_internal_type_lookup_cache_index(type, hash)];
        if (entry->epoch == type->version_tag && entry->type == type && entry->hash == hash && (entry->key == key || tinypy_internal_equal_value(entry->key, key, 1) != 0)) {
            return entry->value;
        }
    }
    for (index = 0U; index < mro_size; ++index) {
        tinypy_type_t *mro_type = __tinypy_internal_type_mro_at_raw(type, index);

        tinypy_value_t *value = tinypy_internal_dict_get_optional(vm, mro_type->dict, key);

        if (value != NULL) {
            if (entry != NULL) {
                TINYPY_INCREF(key);
                if (entry->key != NULL) {
                    TINYPY_DECREF(entry->key);
                }
                entry->hash = hash;
                entry->type = (tinypy_type_t *)type;
                entry->key = key;
                entry->value = value;
                entry->epoch = type->version_tag;
            }
            return value;
        }
    }
    if (entry != NULL) {
        TINYPY_INCREF(key);
        if (entry->key != NULL) {
            TINYPY_DECREF(entry->key);
        }
        entry->hash = hash;
        entry->type = (tinypy_type_t *)type;
        entry->key = key;
        entry->value = NULL;
        entry->epoch = type->version_tag;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_type_get_attr(const tinypy_type_t *type, const char *name, size_t name_size) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_internal_type_lookup_key(vm, type, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_type_set_attr(tinypy_type_t *type, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_internal_type_set_attr_key(type, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_set_attr_key(tinypy_type_t *type, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_vm_t *vm = type->vm;

    if (value->type == &vm->types[TINYPY_VALUE_NATIVE_FUNCTION] && vm->native_method_descriptor_type != NULL && vm->native_wrapper_descriptor_type != NULL) {
        tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

        if (function->self == NULL && function->function == NULL && function->owner == NULL && (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE)) {
            const uint8_t *bytes = TINYPY_TEXT_BYTES(key);
            size_t size = TINYPY_TEXT_BYTE_SIZE(key);
            tinypy_bool_t wrapper = function->descriptor_kind == TINYPY_NATIVE_DESCRIPTOR_WRAPPER
                                        ? TINYPY_TRUE
                                        : function->descriptor_kind == TINYPY_NATIVE_DESCRIPTOR_METHOD
                                              ? TINYPY_FALSE
                                              : __tinypy_internal_native_descriptor_is_wrapper(bytes, size);
            tinypy_type_t *descriptor_type = wrapper != 0 ? vm->native_wrapper_descriptor_type : vm->native_method_descriptor_type;
            tinypy_type_t *previous_type = value->type;

            TINYPY_INCREF(&descriptor_type->base.base);
            value->type = descriptor_type;
            TINYPY_DECREF(&previous_type->base.base);
            function->owner = type;
            function->owner_retained = (type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U ? TINYPY_TRUE : TINYPY_FALSE;
            if (function->owner_retained != 0) {
                TINYPY_INCREF(&type->base.base);
            }
        }
    }
    tinypy_dict_set(type->dict, key, value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t **tinypy_internal_object_dict_slot(tinypy_value_t *value) {
    if (value->type->dict_offset == 0U) {
        return NULL;
    }
    if (__tinypy_internal_type_has_variable_immutable_builtin_layout(TINYPY_VALUE_KIND(value)) != 0) {
        size_t payload_size = tinypy_internal_variable_builtin_payload_size(value);
        size_t aligned_payload = (payload_size + sizeof(tinypy_value_t *) - 1U) & ~(sizeof(tinypy_value_t *) - 1U);

        return (tinypy_value_t **)((uint8_t *)value + aligned_payload) + value->type->slot_count;
    }
    return (tinypy_value_t **)((uint8_t *)value + value->type->dict_offset);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t **tinypy_internal_object_member_slot(tinypy_value_t *value, size_t index) {
    if (__tinypy_internal_type_has_variable_immutable_builtin_layout(TINYPY_VALUE_KIND(value)) != 0) {
        size_t payload_size = tinypy_internal_variable_builtin_payload_size(value);
        size_t aligned_payload = (payload_size + sizeof(tinypy_value_t *) - 1U) & ~(sizeof(tinypy_value_t *) - 1U);

        return (tinypy_value_t **)((uint8_t *)value + aligned_payload) + index;
    }
    return (tinypy_value_t **)((uint8_t *)value + value->type->slots_offset) + index;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

    if (dict_slot != NULL && *dict_slot != NULL) {
        visit(*dict_slot, user_data);
    }
    size_t index;

    for (index = 0U; index < value->type->slot_count; ++index) {
        tinypy_value_t **slot = tinypy_internal_object_member_slot(value, index);

        if (*slot != NULL) {
            visit(*slot, user_data);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_instance_new(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;

    tinypy_value_t *return_value_1 = tinypy_internal_object_allocate(vm, type, type->basic_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_value_t *tinypy_instance_dict(const tinypy_value_t *instance) {
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot((tinypy_value_t *)instance);

    return dict_slot != NULL ? *dict_slot : NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_instance_get_attr(tinypy_value_t *instance_value, const char *name, size_t name_size) {
    tinypy_value_t *value;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance_value);

    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    if (dict_slot != NULL && *dict_slot != NULL) {
        value = tinypy_internal_dict_get_optional(vm, *dict_slot, key);
        if (value != NULL) {
            TINYPY_DECREF(key);
            return value;
        }
    }
    value = tinypy_internal_type_lookup_key(vm, instance_value->type, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_instance_set_attr(tinypy_value_t *instance_value, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_value_t *key = NULL;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance_value);
    if (*dict_slot == NULL) {
        *dict_slot = tinypy_dict_new(vm);
    }
    key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_dict_set(*dict_slot, key, value);
    __tinypy_internal_release_if_not_null(key);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_call_with_first(tinypy_value_t *callable, tinypy_value_t *first, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

    tinypy_value_t *call_args = tinypy_internal_tuple_prepend_checked(vm, first, args, out_error);
    if (call_args == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_call(callable, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_raw_callable(tinypy_value_t *attribute) {
    if (TINYPY_VALUE_KIND(attribute) == TINYPY_VALUE_STATIC_METHOD) {
        tinypy_value_t *return_value_1 = tinypy_static_method_callable(attribute);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(attribute) == TINYPY_VALUE_CLASS_METHOD) {
        tinypy_value_t *return_value_2 = tinypy_class_method_callable(attribute);
        return return_value_2;
    }
    return attribute;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_type_t *type = (tinypy_type_t *)callable;
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *initializer_attribute;
    tinypy_value_t *initializer;
    tinypy_value_t *initialize_result;

    if (vm->exception_types[TINYPY_EXCEPTION_BASE] != NULL && tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0) {
        tinypy_value_t *return_value_1 = tinypy_internal_exception_instantiate(type, args, kwargs, out_error);
        return return_value_1;
    }
    if (type->create != NULL) {
        tinypy_value_t *return_value_2 = type->create(type, args, kwargs, out_error);
        return return_value_2;
    }
    if ((type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin type has no public constructor", out_error);
        return NULL;
    }
    tinypy_value_t *new_attribute = tinypy_internal_type_lookup_key(vm, type, vm->special_new_key);
    if (new_attribute == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class has no __new__", out_error);
        return NULL;
    }
    tinypy_value_t *type_raw_callable = __tinypy_internal_type_raw_callable(new_attribute);
    tinypy_value_t *instance = __tinypy_internal_type_call_with_first(type_raw_callable, &type->base.base, args, kwargs, out_error);
    if (instance == NULL) {
        return NULL;
    }
    if ((type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U) {
        tinypy_value_t *metaclass_initializer = tinypy_internal_type_lookup_key(vm, type, vm->special_init_key);

        if (metaclass_initializer != NULL) {
            tinypy_value_t *type_raw_callable_2 = __tinypy_internal_type_raw_callable(metaclass_initializer);
            tinypy_value_t *metaclass_initialize_result = __tinypy_internal_type_call_with_first(type_raw_callable_2, instance, args, kwargs, out_error);

            if (metaclass_initialize_result == NULL) {
                TINYPY_DECREF(instance);
                return NULL;
            }
            if (TINYPY_VALUE_KIND(metaclass_initialize_result) != TINYPY_VALUE_NONE) {
                TINYPY_DECREF(metaclass_initialize_result);
                TINYPY_DECREF(instance);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "metaclass __init__ must return None", out_error);
                return NULL;
            }
            TINYPY_DECREF(metaclass_initialize_result);
        }
        return instance;
    }
    if (tinypy_type_is_subtype(instance->type, type) == 0) {
        return instance;
    }
    initializer_attribute = tinypy_internal_type_lookup_key(vm, type, vm->special_init_key);
    if (initializer_attribute == NULL) {
        tinypy_value_t *object_new = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], vm->special_new_key);

        if (new_attribute != object_new) {
            return instance;
        }
        if (TINYPY_TUPLE_SIZE(args) != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class constructor takes no arguments", out_error);
            return NULL;
        }
        return instance;
    }
    initializer = tinypy_internal_descriptor_get_value(vm, initializer_attribute, instance, type, out_error);
    if (initializer == NULL) {
        TINYPY_DECREF(instance);
        return NULL;
    }
    initialize_result = tinypy_call(initializer, args, kwargs, out_error);
    TINYPY_DECREF(initializer);
    if (initialize_result == NULL) {
        TINYPY_DECREF(instance);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(initialize_result) != TINYPY_VALUE_NONE) {
        TINYPY_DECREF(initialize_result);
        TINYPY_DECREF(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__init__ must return None", out_error);
        return NULL;
    }
    TINYPY_DECREF(initialize_result);
    return instance;
}
