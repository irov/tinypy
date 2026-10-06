#include "tinypy/type.h"

#include "internal.h"

#include <string.h>
/* MRO and base entries are types or classic classes, so the merge works on
   values and only the layout and metaclass selection see the type entries. */
typedef struct tinypy_mro_sequence_t {
    tinypy_value_t **items;
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
static tinypy_value_t *__tinypy_internal_type_mro_value_at(const tinypy_type_t *type, size_t index) {
    if (type->mro != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(type->mro);

        return items[index];
    }

    const tinypy_type_t *current = type;
    while (index != 0U && current != NULL) {
        current = current->base_type;
        index -= 1U;
    }
    return current != NULL ? (tinypy_value_t *)&current->base.base : NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_refresh_classic_mro(tinypy_type_t *type) {
    type->has_classic_mro = TINYPY_FALSE;
    if (type->mro != NULL) {
        for (size_t index = 0U; index < TINYPY_TUPLE_SIZE(type->mro); ++index) {
            if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(type->mro, index)) == TINYPY_VALUE_CLASS) {
                type->has_classic_mro = TINYPY_TRUE;
                break;
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_mro_entry_dict(tinypy_value_t *entry) {
    tinypy_value_t *dict = TINYPY_VALUE_KIND(entry) == TINYPY_VALUE_CLASS ? TINYPY_CLASS_OBJECT(entry)->dict : ((tinypy_type_t *)entry)->dict;

    return dict;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_mro_entry_name(tinypy_value_t *entry, const char **out_bytes, size_t *out_size) {
    if (TINYPY_VALUE_KIND(entry) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *name = TINYPY_CLASS_OBJECT(entry)->name;

        *out_bytes = (const char *)TINYPY_TEXT_BYTES(name);
        *out_size = TINYPY_TEXT_BYTE_SIZE(name);
        return;
    }
    *out_bytes = ((tinypy_type_t *)entry)->name;
    *out_size = ((tinypy_type_t *)entry)->name_size;
}
//////////////////////////////////////////////////////////////////////////
/* Depth-first, left-to-right linearisation of a classic class, as
   classic_mro produces for a classic base of a new-style class. */
static size_t __tinypy_internal_classic_mro_bound(tinypy_value_t *class_value) {
    tinypy_value_t *bases = TINYPY_CLASS_OBJECT(class_value)->bases;
    size_t count = 1U;
    size_t index;

    for (index = 0U; index < TINYPY_TUPLE_SIZE(bases); ++index) {
        count += __tinypy_internal_classic_mro_bound(TINYPY_TUPLE_GET(bases, index));
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_classic_mro_fill(tinypy_value_t *class_value, tinypy_value_t **items, size_t *in_out_count) {
    tinypy_value_t *bases = TINYPY_CLASS_OBJECT(class_value)->bases;
    size_t index;

    for (index = 0U; index < *in_out_count; ++index) {
        if (items[index] == class_value) {
            return;
        }
    }
    items[*in_out_count] = class_value;
    *in_out_count += 1U;
    for (index = 0U; index < TINYPY_TUPLE_SIZE(bases); ++index) {
        __tinypy_internal_classic_mro_fill(TINYPY_TUPLE_GET(bases, index), items, in_out_count);
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_mro_entry_bound(tinypy_value_t *base) {
    size_t bound = TINYPY_VALUE_KIND(base) == TINYPY_VALUE_CLASS ? __tinypy_internal_classic_mro_bound(base) : __tinypy_internal_type_mro_size_raw((const tinypy_type_t *)base);

    return bound;
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
static tinypy_value_t *__tinypy_internal_type_base_value_at(const tinypy_type_t *type, size_t index) {
    if (type->bases != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(type->bases);

        return items[index];
    }
    return index == 0U && type->base_type != NULL ? (tinypy_value_t *)&type->base_type->base.base : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_remove_subclass(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_value_t *base_value = tinypy_weakref_get((tinypy_value_t *)user_data);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;
    size_t found = SIZE_MAX;
    size_t index;

    (void)kwargs;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *reference = TINYPY_TUPLE_GET(args, 0U);
    if (base_value == NULL) {
        tinypy_value_t *result = tinypy_none_get(vm);
        return result;
    }
    tinypy_type_t *base = (tinypy_type_t *)base_value;
    TINYPY_INCREF(base_value);
    if (base->subclasses == NULL) {
        tinypy_value_t *return_value_1 = tinypy_none_get(base->vm);
        TINYPY_DECREF(base_value);
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
    TINYPY_DECREF(base_value);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_subclass_callback_finalize(void *user_data) {
    TINYPY_DECREF((tinypy_value_t *)user_data);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_add_subclass(tinypy_type_t *base, tinypy_type_t *subclass) {
    tinypy_error_t *error = NULL;

    tinypy_value_t *base_reference = tinypy_weakref_new(&base->base.base, NULL, &error);
    if (base_reference == NULL) {
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_clear_raised(base->vm);
        return;
    }
    tinypy_value_t *callback = tinypy_native_function_new(base->vm, "__remove_subclass", 17U, __tinypy_internal_type_remove_subclass, base_reference, __tinypy_internal_type_subclass_callback_finalize);
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

    if (type == candidate_base) {
        return TINYPY_TRUE;
    }
    if (type->mro == NULL) {
        for (const tinypy_type_t *current = type->base_type; current != NULL; current = current->base_type) {
            if (current == candidate_base) {
                return TINYPY_TRUE;
            }
        }
        return TINYPY_FALSE;
    }
    count = __tinypy_internal_type_mro_size_raw(type);
    for (index = 0U; index < count; ++index) {
        if (__tinypy_internal_type_mro_value_at(type, index) == &candidate_base->base.base) {
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
    tinypy_value_t *entry = __tinypy_internal_type_base_value_at(type, index);
    const tinypy_type_t *return_value_1 = entry != NULL && TINYPY_VALUE_KIND(entry) == TINYPY_VALUE_TYPE ? (const tinypy_type_t *)entry : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_base_value_at(const tinypy_type_t *type, size_t index) {
    tinypy_value_t *entry = __tinypy_internal_type_base_value_at(type, index);

    return entry;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_type_mro_size(const tinypy_type_t *type) {
    size_t return_value_1 = __tinypy_internal_type_mro_size_raw(type);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_mro_at(const tinypy_type_t *type, size_t index) {
    tinypy_value_t *entry = __tinypy_internal_type_mro_value_at(type, index);
    const tinypy_type_t *return_value_1 = entry != NULL && TINYPY_VALUE_KIND(entry) == TINYPY_VALUE_TYPE ? (const tinypy_type_t *)entry : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_mro_value_at(const tinypy_type_t *type, size_t index) {
    tinypy_value_t *entry = __tinypy_internal_type_mro_value_at(type, index);

    return entry;
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
            "metaclass conflict: the metaclass of a derived class must be a (non-strict) subclass of the metaclasses of all its bases", out_error);
        return NULL;
    }

    return winner;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_mro_head_in_tail(tinypy_value_t *candidate, const tinypy_mro_sequence_t *sequences, size_t sequence_count) {
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
static void __tinypy_internal_mro_free(tinypy_vm_t *vm, tinypy_mro_sequence_t *sequences, size_t sequence_size, tinypy_value_t **storage, size_t storage_size, tinypy_value_t **result, size_t result_size) {
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
/* Reports the heads that block the merge, as set_mro_error does. */
static void __tinypy_internal_mro_conflict_error(tinypy_vm_t *vm, const tinypy_mro_sequence_t *sequences, size_t sequence_count, tinypy_error_t **out_error) {
    static const char prefix[] = "Cannot create a consistent method resolution\norder (MRO) for bases";
    size_t part_capacity = 1U + sequence_count * 2U;
    tinypy_message_part_t *parts = (tinypy_message_part_t *)tinypy_internal_vm_allocate(vm, part_capacity * sizeof(*parts));
    size_t part_count = 1U;
    size_t sequence_index;

    parts[0].bytes = prefix;
    parts[0].size = sizeof(prefix) - 1U;
    for (sequence_index = 0U; sequence_index < sequence_count; ++sequence_index) {
        const tinypy_mro_sequence_t *sequence = &sequences[sequence_index];
        tinypy_value_t *head;
        size_t earlier;
        tinypy_bool_t seen = TINYPY_FALSE;

        if (sequence->position == sequence->size) {
            continue;
        }
        head = sequence->items[sequence->position];
        for (earlier = 0U; earlier < sequence_index; ++earlier) {
            if (sequences[earlier].position < sequences[earlier].size && sequences[earlier].items[sequences[earlier].position] == head) {
                seen = TINYPY_TRUE;
                break;
            }
        }
        if (seen != 0) {
            continue;
        }
        parts[part_count].bytes = part_count == 1U ? " " : ", ";
        parts[part_count].size = part_count == 1U ? 1U : 2U;
        part_count += 1U;
        __tinypy_internal_mro_entry_name(head, &parts[part_count].bytes, &parts[part_count].size);
        part_count += 1U;
    }
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, part_count, out_error);
    tinypy_internal_vm_deallocate(vm, parts, part_capacity * sizeof(*parts));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t **__tinypy_internal_c3_merge(tinypy_vm_t *vm, tinypy_value_t *const *bases, size_t base_count, size_t *out_count, size_t *out_allocation_size, tinypy_error_t **out_error) {
    tinypy_mro_sequence_t *sequences = NULL;
    tinypy_value_t **storage = NULL;
    tinypy_value_t **result = NULL;
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
        total_items += __tinypy_internal_mro_entry_bound(bases[index]);
    }
    storage_size = total_items * sizeof(*storage);
    result_capacity = total_items + 1U;
    result_size = result_capacity * sizeof(*result);

    sequences = (tinypy_mro_sequence_t *)tinypy_internal_vm_allocate(
        vm, sequence_size);
    storage = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, storage_size);
    result = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, result_size);
    (void)memset(sequences, 0, sequence_size);
    result[0] = NULL;

    for (index = 0U; index < base_count; ++index) {
        tinypy_mro_sequence_t *sequence = &sequences[index];

        sequence->items = &storage[storage_offset];
        if (TINYPY_VALUE_KIND(bases[index]) == TINYPY_VALUE_CLASS) {
            sequence->size = 0U;
            __tinypy_internal_classic_mro_fill(bases[index], sequence->items, &sequence->size);
        }
        else {
            size_t mro_index;

            sequence->size = __tinypy_internal_type_mro_size_raw((const tinypy_type_t *)bases[index]);
            for (mro_index = 0U; mro_index < sequence->size; ++mro_index) {
                sequence->items[mro_index] = __tinypy_internal_type_mro_value_at((const tinypy_type_t *)bases[index], mro_index);
            }
        }
        storage_offset += sequence->size;
    }
    sequences[base_count].items = &storage[storage_offset];
    sequences[base_count].size = base_count;
    for (index = 0U; index < base_count; ++index) {
        sequences[base_count].items[index] = bases[index];
    }

    for (;;) {
        tinypy_value_t *candidate = NULL;
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
            __tinypy_internal_mro_conflict_error(vm, sequences, sequence_count, out_error);
            __tinypy_internal_mro_free(
                vm, sequences, sequence_size, storage, storage_size,
                result, result_size);
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
static tinypy_bool_t __tinypy_internal_validate_bases(tinypy_vm_t *vm, tinypy_value_t *const *bases, size_t base_count, tinypy_error_t **out_error) {
    size_t index;
    tinypy_bool_t has_type = TINYPY_FALSE;

    for (index = 0U; index < base_count; ++index) {
        tinypy_value_t *base = bases[index];
        size_t earlier;

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE && TINYPY_VALUE_KIND(base) != TINYPY_VALUE_CLASS) {
            __tinypy_internal_type_error(vm, "bases must be types", out_error);
            return TINYPY_FALSE;
        }
        for (earlier = 0U; earlier < index; ++earlier) {
            if (bases[earlier] == base) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("duplicate base class "),
                    {NULL, 0U},
                };

                __tinypy_internal_mro_entry_name(base, &parts[1].bytes, &parts[1].size);
                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
                return TINYPY_FALSE;
            }
        }
        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE) {
            continue;
        }
        has_type = TINYPY_TRUE;
        if ((((const tinypy_type_t *)base)->flags & TINYPY_TYPE_FLAG_BASE_TYPE) == 0U) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("type '"),
                {((const tinypy_type_t *)base)->name, ((const tinypy_type_t *)base)->name_size},
                TINYPY_MESSAGE_PART_LITERAL("' is not an acceptable base type"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            return TINYPY_FALSE;
        }
    }
    if (has_type == 0) {
        __tinypy_internal_type_error(vm, "a new-style class can't have only classic bases", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Collects the type entries of a validated base list for layout and
   metaclass selection. */
static const tinypy_type_t **__tinypy_internal_collect_type_bases(tinypy_vm_t *vm, tinypy_value_t *const *bases, size_t base_count, size_t *out_count) {
    const tinypy_type_t **types = (const tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*types));
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < base_count; ++index) {
        if (TINYPY_VALUE_KIND(bases[index]) == TINYPY_VALUE_TYPE) {
            types[count] = (const tinypy_type_t *)bases[index];
            count += 1U;
        }
    }
    *out_count = count;
    return types;
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
static size_t __tinypy_internal_rebase_find(const tinypy_rebase_record_t *records, size_t count, const tinypy_value_t *entry) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (&records[index].type->base.base == entry) {
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
            if (__tinypy_internal_rebase_find(records, count, subclass_value) != SIZE_MAX) {
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
    tinypy_value_t **bases = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*bases));
    tinypy_value_t **merged;
    tinypy_value_t *mro;
    size_t merged_count;
    size_t merged_size;
    size_t index;

    for (index = 0U; index < base_count; ++index) {
        bases[index] = __tinypy_internal_type_base_value_at(type, index);
    }
    merged = __tinypy_internal_c3_merge(vm, bases, base_count, &merged_count, &merged_size, out_error);
    tinypy_internal_vm_deallocate(vm, bases, base_count * sizeof(*bases));
    if (merged == NULL) {
        return NULL;
    }
    merged[0] = &type->base.base;
    mro = tinypy_internal_tuple_from_borrowed_items(vm, merged, merged_count + 1U);
    tinypy_internal_vm_deallocate(vm, merged, merged_size);
    return mro;
}
//////////////////////////////////////////////////////////////////////////
/* equiv_structs in Python 2.7: two types whose instances share one layout. */
static tinypy_bool_t __tinypy_internal_type_equivalent_layout(const tinypy_type_t *left, const tinypy_type_t *right) {
    if (left == right) {
        return TINYPY_TRUE;
    }
    if (left == NULL || right == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t equivalent = left->layout_kind == right->layout_kind
                               && left->basic_size == right->basic_size
                               && left->item_size == right->item_size
                               && left->dict_offset == right->dict_offset
                               && left->weakref_offset == right->weakref_offset
                               && left->slots_offset == right->slots_offset
                               && left->slot_count == right->slot_count
                               && left->release_references == right->release_references
                               && left->destroy == right->destroy
                                   ? TINYPY_TRUE
                                   : TINYPY_FALSE;

    return equivalent;
}
//////////////////////////////////////////////////////////////////////////
/* same_slots_added in Python 2.7: siblings that add identical slots. */
static tinypy_bool_t __tinypy_internal_type_same_slots_added(const tinypy_type_t *left, const tinypy_type_t *right) {
    tinypy_vm_t *vm = left->vm;
    tinypy_value_t *key;
    tinypy_value_t *left_slots;
    tinypy_value_t *right_slots;
    tinypy_bool_t same = TINYPY_TRUE;

    if (left->basic_size != right->basic_size || left->slot_count != right->slot_count || left->dict_offset != right->dict_offset || left->weakref_offset != right->weakref_offset) {
        return TINYPY_FALSE;
    }
    key = tinypy_string_from_bytes(vm, "__slots__", 9U);
    left_slots = tinypy_internal_dict_get_optional(vm, left->dict, key);
    right_slots = tinypy_internal_dict_get_optional(vm, right->dict, key);
    TINYPY_DECREF(key);
    if (left_slots != NULL && right_slots != NULL) {
        same = tinypy_compare_bool(left_slots, right_slots, TINYPY_COMPARE_EQUAL, NULL) > 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    return same;
}
//////////////////////////////////////////////////////////////////////////
/* compatible_for_assignment in Python 2.7, shared by __class__ and __bases__
   assignment; the deallocator test maps to the heap/static distinction. */
tinypy_bool_t tinypy_internal_type_layout_compatible(const tinypy_type_t *old_type, const tinypy_type_t *new_type, const char *attribute, size_t attribute_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = old_type->vm;
    const tinypy_type_t *new_base = new_type;
    const tinypy_type_t *old_base = old_type;
    const char *reason = NULL;

    if (((old_type->flags ^ new_type->flags) & TINYPY_TYPE_FLAG_HEAP) != 0U || old_type->destroy != new_type->destroy) {
        reason = "' deallocator differs from '";
    }
    else {
        while (__tinypy_internal_type_equivalent_layout(new_base, new_base->base_type)) {
            new_base = new_base->base_type;
        }
        while (__tinypy_internal_type_equivalent_layout(old_base, old_base->base_type)) {
            old_base = old_base->base_type;
        }
        if (new_base != old_base && (new_base->base_type != old_base->base_type || __tinypy_internal_type_same_slots_added(new_base, old_base) == 0)) {
            reason = "' object layout differs from '";
        }
    }
    if (reason == NULL) {
        return TINYPY_TRUE;
    }
    tinypy_message_part_t parts[] = {
        {attribute, attribute_size},
        TINYPY_MESSAGE_PART_LITERAL(" assignment: '"),
        {new_type->name, new_type->name_size},
        {reason, strlen(reason)},
        {old_type->name, old_type->name_size},
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_rebase_has_entry(tinypy_value_t *bases, const tinypy_value_t *entry) {
    size_t index;

    for (index = 0U; index < TINYPY_TUPLE_SIZE(bases); ++index) {
        if (TINYPY_TUPLE_GET(bases, index) == entry) {
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
    tinypy_value_t *const *bases;
    const tinypy_type_t **type_bases = NULL;
    size_t type_base_count = 0U;
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
    bases = TINYPY_TUPLE_ITERATOR_BEGIN(bases_value);
    for (index = 0U; index < base_count; ++index) {
        tinypy_value_t *base_value = bases[index];

        if (TINYPY_VALUE_KIND(base_value) != TINYPY_VALUE_TYPE && TINYPY_VALUE_KIND(base_value) != TINYPY_VALUE_CLASS) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("__bases__ must be tuple of old- or new-style classes, not '"),
                TINYPY_MESSAGE_PART_TYPE_NAME(base_value),
                TINYPY_MESSAGE_PART_LITERAL("'"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            return TINYPY_FALSE;
        }
        if (base_value == &type->base.base || (TINYPY_VALUE_KIND(base_value) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)base_value, type) != 0)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a __bases__ item causes an inheritance cycle", out_error);
            return TINYPY_FALSE;
        }
    }
    if (__tinypy_internal_validate_bases(vm, bases, base_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    type_bases = __tinypy_internal_collect_type_bases(vm, bases, base_count, &type_base_count);
    layout_base = __tinypy_internal_select_layout_base(vm, type_bases, type_base_count, out_error);
    if (layout_base == NULL || tinypy_internal_type_layout_compatible(type->base_type, layout_base, "__bases__", 9U, out_error) == 0) {
        tinypy_internal_vm_deallocate(vm, type_bases, base_count * sizeof(*type_bases));
        return TINYPY_FALSE;
    }
    metaclass = __tinypy_internal_select_metaclass(vm, type_bases, type_base_count, type->base.base.type, out_error);
    tinypy_internal_vm_deallocate(vm, type_bases, base_count * sizeof(*type_bases));
    if (metaclass == NULL || metaclass != type->base.base.type) {
        if (metaclass != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__bases__ assignment would change the metaclass", out_error);
        }
        return TINYPY_FALSE;
    }

    records = __tinypy_internal_rebase_collect(type, &record_count, &record_capacity, out_error);
    if (records == NULL) {
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
                tinypy_value_t *candidate_base = __tinypy_internal_type_base_value_at(record->type, base_index);
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
            __tinypy_internal_type_refresh_classic_mro(record->type);
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
        tinypy_value_t *old_base = TINYPY_TUPLE_GET(old_bases, index);

        if (TINYPY_VALUE_KIND(old_base) == TINYPY_VALUE_TYPE && __tinypy_internal_rebase_has_entry(bases_value, old_base) == 0) {
            __tinypy_internal_rebase_remove_subclass((tinypy_type_t *)old_base, type);
        }
    }
    for (index = 0U; index < base_count; ++index) {
        if (TINYPY_VALUE_KIND(bases[index]) == TINYPY_VALUE_TYPE && __tinypy_internal_rebase_has_entry(old_bases, bases[index]) == 0) {
            __tinypy_internal_type_add_subclass((tinypy_type_t *)bases[index], type);
        }
    }
    type->base_type = (tinypy_type_t *)layout_base;
    tinypy_internal_type_lookup_cache_invalidate(vm);
    for (index = 0U; index < record_count; ++index) {
        tinypy_rebase_record_t *record = &records[index];

        __tinypy_internal_rebase_destroy_mro(vm, record->old_mro);
        record->type->version_tag = vm->type_lookup_cache_epoch;
        record->type->has_finalizer = tinypy_internal_type_lookup_key(vm, record->type, vm->special_del_key) != NULL ? INT32_C(1) : INT32_C(0);
    }
    TINYPY_DECREF(old_bases);
    __tinypy_internal_rebase_release_records(vm, records, record_count, record_capacity);
    return TINYPY_TRUE;

rollback:
    type->bases = old_bases;
    TINYPY_DECREF(bases_value);
    for (index = 0U; index < record_count; ++index) {
        tinypy_rebase_record_t *record = &records[index];

        if (record->new_mro != NULL) {
            record->type->mro = record->old_mro;
            __tinypy_internal_type_refresh_classic_mro(record->type);
            __tinypy_internal_rebase_destroy_mro(vm, record->new_mro);
        }
    }
    __tinypy_internal_rebase_release_records(vm, records, record_count, record_capacity);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, int32_t configured_instance_dict, int32_t configured_weakrefs, tinypy_error_t **out_error) {
    tinypy_value_t *default_base = NULL;
    tinypy_value_t *const *actual_bases = bases;
    size_t actual_base_count = base_count;
    const tinypy_type_t **type_bases = NULL;
    size_t type_base_count = 0U;
    tinypy_type_t *metaclass = NULL;
    tinypy_value_t **mro_types = NULL;
    size_t mro_tail_count = 0U;
    size_t mro_workspace_size = 0U;
    tinypy_type_t *type = NULL;
    tinypy_value_t *name_object = NULL;
    tinypy_value_t *dict = NULL;
    tinypy_value_t *bases_tuple = NULL;
    tinypy_value_t *mro_tuple = NULL;
    tinypy_value_t *own_slots = NULL;
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
        default_base = &vm->types[TINYPY_VALUE_INSTANCE].base.base;
        actual_bases = &default_base;
        actual_base_count = 1U;
    }
    if (__tinypy_internal_validate_bases(
            vm, actual_bases, actual_base_count, out_error) == 0) {
        return NULL;
    }
    type_bases = __tinypy_internal_collect_type_bases(vm, actual_bases, actual_base_count, &type_base_count);
    const tinypy_type_t *layout_base = __tinypy_internal_select_layout_base(vm, type_bases, type_base_count, out_error);
    if (layout_base != NULL) {
        metaclass = __tinypy_internal_select_metaclass(
            vm, type_bases, type_base_count,
            explicit_metaclass, out_error);
    }
    tinypy_internal_vm_deallocate(vm, type_bases, actual_base_count * sizeof(*type_bases));
    if (layout_base == NULL || metaclass == NULL) {
        return NULL;
    }
    mro_types = __tinypy_internal_c3_merge(
        vm, actual_bases, actual_base_count,
        &mro_tail_count, &mro_workspace_size, out_error);
    if (mro_types == NULL) {
        return NULL;
    }
    /* The type owns a private copy of the namespace, as type_new does in
       Python 2.7: the caller's dictionary must stay independent of the type. */
    dict = tinypy_dict_new(vm);
    if (namespace_dict != NULL && tinypy_internal_dict_update_from(dict, namespace_dict, out_error) == 0) {
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size);
        return NULL;
    }
    tinypy_value_t *module_key = tinypy_string_from_bytes(vm, "__module__", 10U);
    tinypy_value_t *doc_key = tinypy_string_from_bytes(vm, "__doc__", 7U);
    tinypy_value_t *module = vm->current_frame != NULL ? tinypy_internal_dict_get_optional(vm, vm->current_frame->globals, vm->special_name_key) : NULL;
    if (module != NULL) {
        TINYPY_INCREF(module);
    }
    tinypy_bool_t has_module = TINYPY_FALSE;
    tinypy_bool_t has_doc = TINYPY_FALSE;
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_bool_t metadata_ok = tinypy_internal_dict_contains_checked(vm, dict, module_key, &has_module, out_error);
    if (metadata_ok != 0) {
        metadata_ok = tinypy_internal_dict_contains_checked(vm, dict, doc_key, &has_doc, out_error);
    }
    if (metadata_ok != 0 && has_module == 0 && module != NULL) {
        metadata_ok = tinypy_internal_dict_set_checked(vm, dict, module_key, module, out_error);
    }
    if (metadata_ok != 0 && has_doc == 0) {
        metadata_ok = tinypy_internal_dict_set_checked(vm, dict, doc_key, none, out_error);
    }
    TINYPY_DECREF(none);
    if (module != NULL) {
        TINYPY_DECREF(module);
    }
    TINYPY_DECREF(doc_key);
    TINYPY_DECREF(module_key);
    if (metadata_ok == 0) {
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size);
        return NULL;
    }
    tinypy_value_t *new_method = tinypy_internal_dict_get_optional(vm, dict, vm->special_new_key);

    if (new_method != NULL && TINYPY_VALUE_KIND(new_method) == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *descriptor = tinypy_static_method_new(new_method);

        tinypy_dict_set(dict, vm->special_new_key, descriptor);
        TINYPY_DECREF(descriptor);
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
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "nonempty __slots__ not supported for subtype of 'tuple'", out_error);
        return NULL;
    }
    if ((dict_slot != 0 && layout_base->has_instance_dict != 0) || (weakref_slot != 0 && layout_base->weakref_offset != 0U)) {
        TINYPY_DECREF(own_slots);
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, dict_slot != 0 && layout_base->has_instance_dict != 0 ? "__dict__ slot disallowed: we already got one" : "__weakref__ slot disallowed: either we already got one, or __itemsize__ != 0", out_error);
        return NULL;
    }
    instance_kind = tinypy_type_is_subtype(layout_base, &vm->types[TINYPY_VALUE_TYPE]) != 0
                        ? TINYPY_VALUE_TYPE
                        : (layout_base->layout_kind != TINYPY_VALUE_INVALID ? layout_base->layout_kind : TINYPY_VALUE_INSTANCE);
    type = (tinypy_type_t *)tinypy_internal_object_allocate(
        vm, metaclass, sizeof(*type));

    type->vm = vm;
    vm->type_lookup_cache_epoch += UINT64_C(1);
    if (vm->type_lookup_cache_epoch == 0U) {
        vm->type_lookup_cache_epoch = UINT64_C(1);
    }
    type->version_tag = vm->type_lookup_cache_epoch;
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

    bases_tuple = tinypy_tuple_from_items(
        vm, actual_bases, actual_base_count);

    mro_types[0] = &type->base.base;
    mro_tuple = tinypy_internal_tuple_from_borrowed_items(
        vm, mro_types, mro_tail_count + 1U);

    type->name_object = name_object;
    type->dict = dict;
    TINYPY_DICT_OBJECT(type->dict)->type_dictionary = INT32_C(1);
    TINYPY_DICT_OBJECT(type->dict)->type_owner = type;
    type->bases = bases_tuple;
    type->mro = mro_tuple;
    __tinypy_internal_type_refresh_classic_mro(type);
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
            continue;
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
    type->has_finalizer = vm->special_del_key != NULL && tinypy_internal_type_lookup_key(vm, type, vm->special_del_key) != NULL ? INT32_C(1) : INT32_C(0);

    tinypy_internal_vm_deallocate(
        vm, mro_types, mro_workspace_size);
    for (index = 0U; index < actual_base_count; ++index) {
        if (TINYPY_VALUE_KIND(actual_bases[index]) == TINYPY_VALUE_TYPE) {
            __tinypy_internal_type_add_subclass((tinypy_type_t *)actual_bases[index], type);
        }
    }
    return type;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_type_new_from_types(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, int32_t configured_instance_dict, int32_t configured_weakrefs, tinypy_error_t **out_error) {
    tinypy_value_t **values = NULL;
    tinypy_type_t *result;
    size_t index;

    if (base_count != 0U) {
        values = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*values));
        for (index = 0U; index < base_count; ++index) {
            values[index] = (tinypy_value_t *)&bases[index]->base.base;
        }
    }
    result = __tinypy_internal_type_new(vm, name, name_size, values, base_count, explicit_metaclass, namespace_dict, configured_instance_dict, configured_weakrefs, out_error);
    if (values != NULL) {
        tinypy_internal_vm_deallocate(vm, values, base_count * sizeof(*values));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Creates a Python-visible class from a bases tuple whose entries may be
   classic classes, the way type(name, bases, dict) accepts them. */
tinypy_type_t *tinypy_internal_type_new_from_values(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_type_t *result = __tinypy_internal_type_new(vm, name, name_size, bases, base_count, explicit_metaclass, namespace_dict, -1, -1, out_error);

    if (result != NULL) {
        result->flags |= TINYPY_TYPE_FLAG_PYTHON_HEAP;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A dying type may leave its dictionary and the descriptors it created behind
   (a dictproxy or `A.__dict__['x']` can outlive the class), so those lose their
   back-pointers before the type's references are released. */
void tinypy_internal_type_detach(tinypy_type_t *type) {
    tinypy_dict_object_t *dict;
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    if (type->dict == NULL) {
        return;
    }
    dict = TINYPY_DICT_OBJECT(type->dict);
    if (dict->type_owner == type) {
        dict->type_dictionary = INT32_C(0);
        dict->type_owner = NULL;
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(type->dict);
    iterator_end = TINYPY_DICT_ITERATOR_END(type->dict);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_type_e kind;

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            continue;
        }
        kind = TINYPY_VALUE_KIND(iterator->value);
        if (kind == TINYPY_VALUE_MEMBER_DESCRIPTOR || kind == TINYPY_VALUE_GETSET_DESCRIPTOR) {
            tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(iterator->value);

            if (descriptor->owner == type && descriptor->owner_retained == 0) {
                descriptor->owner = NULL;
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_type_t *result = __tinypy_internal_type_new_from_types(vm, name, name_size, bases, base_count, explicit_metaclass, namespace_dict, -1, -1, out_error);

    if (result != NULL) {
        result->flags |= TINYPY_TYPE_FLAG_PYTHON_HEAP;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_internal_type_new_configured(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_bool_t has_instance_dict, tinypy_bool_t has_weakrefs, tinypy_error_t **out_error) {
    tinypy_type_t *result = __tinypy_internal_type_new_from_types(vm, name, name_size, bases, base_count, explicit_metaclass, namespace_dict, has_instance_dict != 0 ? 1 : 0, has_weakrefs != 0 ? 1 : 0, out_error);

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
    type->version_tag = epoch;
    if (type->vm->special_del_key != NULL) {
        type->has_finalizer = tinypy_internal_type_lookup_key(type->vm, type, type->vm->special_del_key) != NULL ? INT32_C(1) : INT32_C(0);
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

    if (type->has_classic_mro == 0 && __tinypy_internal_type_lookup_cacheable(vm, key) != 0) {
        hash = tinypy_internal_hash_value(key, NULL);
        entry = &vm->type_lookup_cache[__tinypy_internal_type_lookup_cache_index(type, hash)];
        if (entry->epoch == type->version_tag && entry->type == type && entry->hash == hash && (entry->key == key || tinypy_internal_equal_value(entry->key, key, 1) != 0)) {
            return entry->value;
        }
    }
restart_lookup:
    ;
    tinypy_value_t *mro = type->mro;
    uint64_t version = type->version_tag;
    tinypy_value_t *snapshot = mro != NULL ? tinypy_tuple_from_items(vm, TINYPY_TUPLE_ITEMS(mro), TINYPY_TUPLE_SIZE(mro)) : NULL;
    mro_size = snapshot != NULL ? TINYPY_TUPLE_SIZE(snapshot) : __tinypy_internal_type_mro_size_raw(type);
    for (index = 0U; index < mro_size; ++index) {
        tinypy_value_t *mro_entry = snapshot != NULL ? TINYPY_TUPLE_GET(snapshot, index) : __tinypy_internal_type_mro_value_at(type, index);
        tinypy_value_t *value = tinypy_internal_dict_get_optional(vm, tinypy_internal_type_mro_entry_dict(mro_entry), key);

        if (type->mro != mro) {
            if (snapshot != NULL) {
                TINYPY_DECREF(snapshot);
            }
            goto restart_lookup;
        }
        if (type->version_tag != version) {
            entry = NULL;
        }

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
            if (snapshot != NULL) {
                TINYPY_DECREF(snapshot);
            }
            return value;
        }
    }
    if (snapshot != NULL) {
        TINYPY_DECREF(snapshot);
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
    tinypy_internal_type_install_attr_key(type, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_set_attr_key(tinypy_type_t *type, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_dict_set(type->dict, key, value);
}
//////////////////////////////////////////////////////////////////////////
/* Registration through the C API turns a fresh native function into a method
   descriptor of the type; a plain assignment from Python keeps builtins such
   as len unbound, as CPython does. */
void tinypy_internal_type_install_attr_key(tinypy_type_t *type, tinypy_value_t *key, tinypy_value_t *value) {
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
    /* As in type_call, an object that is not an instance of the type is
       returned without initialisation. */
    if (tinypy_type_is_subtype(instance->type, type) == 0) {
        return instance;
    }
    initializer_attribute = tinypy_internal_type_lookup_key(vm, instance->type, vm->special_init_key);
    if (initializer_attribute == NULL) {
        tinypy_value_t *object_new = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], vm->special_new_key);

        if (new_attribute != object_new) {
            return instance;
        }
        if (TINYPY_TUPLE_SIZE(args) != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object() takes no parameters", out_error);
            return NULL;
        }
        return instance;
    }
    if (TINYPY_VALUE_KIND(initializer_attribute) == TINYPY_VALUE_FUNCTION) {
        /* A Python __init__ is entered directly with the instance prepended,
           saving the bound method and the argument tuple. */
        size_t count = TINYPY_TUPLE_SIZE(args);
        tinypy_value_t *stack_items[8];
        tinypy_value_t **items = stack_items;
        size_t items_size = 0U;

        if (count + 1U > sizeof(stack_items) / sizeof(stack_items[0])) {
            items_size = (count + 1U) * sizeof(*items);
            items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, items_size);
        }
        items[0] = instance;
        if (count != 0U) {
            (void)memcpy(items + 1, TINYPY_TUPLE_ITERATOR_BEGIN(args), count * sizeof(*items));
        }
        initialize_result = tinypy_internal_eval_function_items(initializer_attribute, items, count + 1U, kwargs, out_error);
        if (items != stack_items) {
            tinypy_internal_vm_deallocate(vm, items, items_size);
        }
    }
    else {
        initializer = tinypy_internal_descriptor_get_value(vm, initializer_attribute, instance, instance->type, out_error);
        if (initializer == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        initialize_result = tinypy_call(initializer, args, kwargs, out_error);
        TINYPY_DECREF(initializer);
    }
    if (initialize_result == NULL) {
        TINYPY_DECREF(instance);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(initialize_result) != TINYPY_VALUE_NONE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__init__() should return None, not '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(initialize_result),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(initialize_result);
        TINYPY_DECREF(instance);
        return NULL;
    }
    TINYPY_DECREF(initialize_result);
    return instance;
}
