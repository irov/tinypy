#include "tinypy/type.h"

#include "internal.h"

#include <string.h>
typedef struct tinypy_mro_sequence_t {
    tinypy_type_t **items;
    size_t size;
    size_t position;
} tinypy_mro_sequence_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_error(tinypy_vm_t *vm, const char *message, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
}

#if defined(TINYPY_ENABLE_ASSERTS)
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_type_valid(const tinypy_type_t *type) {
    return type != NULL && tinypy_internal_vm_valid(type->vm) && type->base.base.type != NULL && (type->base.base.type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U;
}
#endif
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_type_mro_size_raw(const tinypy_type_t *type) {
    size_t count = 0U;

    if (type->mro != NULL) {
        return TINYPY_SIZED_SIZE(type->mro);
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
        return TINYPY_SIZED_SIZE(type->bases);
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
    TINYPY_ASSERT(kwargs == NULL || TINYPY_DICT_SIZE(kwargs) == 0U);
    TINYPY_ASSERT(TINYPY_TUPLE_SIZE(args) == 1U);
    TINYPY_ASSERT(base != NULL);
    tinypy_value_t *reference = TINYPY_TUPLE_GET(args, 0U);
    if (base->subclasses == NULL) {
        return tinypy_none_get(base->vm);
    }
    size = TINYPY_TUPLE_SIZE(base->subclasses);
    for (index = 0U; index < size; ++index) {
        if (TINYPY_TUPLE_GET(base->subclasses, index) == reference) {
            found = index;
            break;
        }
    }
    if (found != SIZE_MAX) {
        tinypy_value_t *previous = base->subclasses;

        if (size == 1U) {
            base->subclasses = NULL;
        }
        else {
            tinypy_value_t **items;
            size_t output_index = 0U;

            items = (tinypy_value_t **)tinypy_internal_vm_allocate(base->vm, (size - 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            for (index = 0U; index < size; ++index) {
                if (index != found) {
                    items[output_index++] = TINYPY_TUPLE_GET(previous, index);
                }
            }
            base->subclasses = tinypy_tuple_from_items(base->vm, items, size - 1U);
            tinypy_internal_vm_deallocate(base->vm, items, (size - 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        }
        TINYPY_DECREF(previous);
    }
    return tinypy_none_get(base->vm);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_add_subclass(tinypy_type_t *base, tinypy_type_t *subclass) {
    tinypy_error_t *error = NULL;
    tinypy_value_t **items;
    tinypy_value_t *subclasses;
    size_t size = base->subclasses != NULL ? TINYPY_TUPLE_SIZE(base->subclasses) : 0U;
    size_t index;

    tinypy_value_t *callback = tinypy_native_function_new(base->vm, "__remove_subclass", 17U, __tinypy_internal_type_remove_subclass, base, NULL);
    tinypy_value_t *reference = tinypy_weakref_new(&subclass->base.base, callback, &error);
    TINYPY_DECREF(callback);
    TINYPY_ASSERT(reference != NULL);
    TINYPY_ASSERT(error == NULL);
    TINYPY_ASSERT(size < SIZE_MAX / sizeof(*items));
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(base->vm, (size + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < size; ++index) {
        items[index] = TINYPY_TUPLE_GET(base->subclasses, index);
    }
    items[size] = reference;
    subclasses = tinypy_tuple_from_items(base->vm, items, size + 1U);
    tinypy_internal_vm_deallocate(base->vm, items, (size + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (base->subclasses != NULL) {
        TINYPY_DECREF(base->subclasses);
    }
    base->subclasses = subclasses;
    TINYPY_DECREF(reference);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_subclasses(tinypy_type_t *type) {
    tinypy_value_t *result = tinypy_list_from_items(type->vm, NULL, 0U);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    if (type->subclasses == NULL) {
        return result;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(type->subclasses);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(type->subclasses);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *subclass = tinypy_weakref_get(*iterator);

        if (subclass != NULL) {
            tinypy_list_append(result, subclass);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_type_is_subtype(const tinypy_type_t *type, const tinypy_type_t *candidate_base) {
    size_t count;
    size_t index;

    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    TINYPY_ASSERT(__tinypy_internal_type_valid(candidate_base));
    TINYPY_ASSERT(type->vm == candidate_base->vm);

    count = __tinypy_internal_type_mro_size_raw(type);
    for (index = 0U; index < count; ++index) {
        if (__tinypy_internal_type_mro_at_raw(type, index) == candidate_base) {
            return 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_type_as_value(tinypy_type_t *type) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    return &type->base.base;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_value_t *tinypy_type_as_const_value(const tinypy_type_t *type) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    return &type->base.base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_value_as_type(tinypy_value_t *value) {
    TINYPY_ASSERT(value != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE);
    return (tinypy_type_t *)value;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_value_as_const_type(const tinypy_value_t *value) {
    TINYPY_ASSERT(value != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE);
    return (const tinypy_type_t *)value;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_type_bases_size(const tinypy_type_t *type) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    return __tinypy_internal_type_bases_size_raw(type);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_base_at(const tinypy_type_t *type, size_t index) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    TINYPY_ASSERT(index < __tinypy_internal_type_bases_size_raw(type));
    return __tinypy_internal_type_base_at_raw(type, index);
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_type_mro_size(const tinypy_type_t *type) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    return __tinypy_internal_type_mro_size_raw(type);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_mro_at(const tinypy_type_t *type, size_t index) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    TINYPY_ASSERT(index < __tinypy_internal_type_mro_size_raw(type));
    return __tinypy_internal_type_mro_at_raw(type, index);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_internal_select_metaclass(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_error_t **out_error) {
    tinypy_type_t *winner;
    size_t index;

    if (explicit_metaclass != NULL) {
        TINYPY_ASSERT(__tinypy_internal_type_valid(explicit_metaclass));
        TINYPY_ASSERT(explicit_metaclass->vm == vm);
        if (!tinypy_type_is_subtype(explicit_metaclass, &vm->type_type)) {
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
static int32_t __tinypy_internal_mro_head_in_tail(tinypy_type_t *candidate, const tinypy_mro_sequence_t *sequences, size_t sequence_count) {
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
                return 1;
            }
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_mro_free(tinypy_vm_t *vm, tinypy_mro_sequence_t *sequences, size_t sequence_size, tinypy_type_t **storage, size_t storage_size, tinypy_type_t **result, size_t result_size) {
    if (result != NULL) {
        tinypy_internal_vm_deallocate(
            vm, result, result_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    }
    if (storage != NULL) {
        tinypy_internal_vm_deallocate(
            vm, storage, storage_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    }
    if (sequences != NULL) {
        tinypy_internal_vm_deallocate(
            vm, sequences, sequence_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
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

    TINYPY_ASSERT(base_count != SIZE_MAX);
    sequence_count = base_count + 1U;
    TINYPY_ASSERT(sequence_count <= SIZE_MAX / sizeof(*sequences));
    sequence_size = sequence_count * sizeof(*sequences);

    for (index = 0U; index < base_count; ++index) {
        size_t base_mro_size = __tinypy_internal_type_mro_size_raw(bases[index]);

        TINYPY_ASSERT(total_items <= SIZE_MAX - base_mro_size);
        total_items += base_mro_size;
    }
    TINYPY_ASSERT(total_items <= SIZE_MAX / sizeof(*storage));
    TINYPY_ASSERT(total_items != SIZE_MAX);
    storage_size = total_items * sizeof(*storage);
    result_capacity = total_items + 1U;
    TINYPY_ASSERT(result_capacity <= SIZE_MAX / sizeof(*result));
    result_size = result_capacity * sizeof(*result);

    sequences = (tinypy_mro_sequence_t *)tinypy_internal_vm_allocate(
        vm, sequence_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    storage = (tinypy_type_t **)tinypy_internal_vm_allocate(
        vm, storage_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    result = (tinypy_type_t **)tinypy_internal_vm_allocate(
        vm, result_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
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
        int32_t has_items = 0;
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
        vm, storage, storage_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    tinypy_internal_vm_deallocate(
        vm, sequences, sequence_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    *out_count = result_count;
    *out_allocation_size = result_size;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_validate_bases(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, tinypy_error_t **out_error) {
    size_t index;

    for (index = 0U; index < base_count; ++index) {
        size_t earlier;

        TINYPY_ASSERT(__tinypy_internal_type_valid(bases[index]));
        TINYPY_ASSERT(bases[index]->vm == vm);
        if ((bases[index]->flags & TINYPY_TYPE_FLAG_BASE_TYPE) == 0U) {
            __tinypy_internal_type_error(
                vm,
                "selected type does not permit subclassing", out_error);
            return INT32_C(0);
        }
        for (earlier = 0U; earlier < index; ++earlier) {
            if (bases[earlier] == bases[index]) {
                __tinypy_internal_type_error(
                    vm, "duplicate type base", out_error);
                return INT32_C(0);
            }
        }
    }
    return INT32_C(1);
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
static int32_t __tinypy_internal_type_slot_name_equal(tinypy_value_t *value, const char *name, size_t name_size) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);

    return size == name_size && (size == 0U || memcmp(bytes, name, size) == 0) ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_mangle_slot_name(tinypy_vm_t *vm, const char *class_name, size_t class_name_size, tinypy_value_t *slot_name) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(slot_name);
    size_t size = TINYPY_TEXT_BYTE_SIZE(slot_name);
    size_t class_start = 0U;
    uint8_t *mangled;
    size_t mangled_size;

    if (size < 3U || bytes[0] != '_' || bytes[1] != '_' || (bytes[size - 2U] == '_' && bytes[size - 1U] == '_')) {
        return tinypy_string_from_bytes(vm, bytes, size);
    }
    while (class_start < class_name_size && class_name[class_start] == '_') {
        class_start += 1U;
    }
    if (class_start == class_name_size) {
        return tinypy_string_from_bytes(vm, bytes, size);
    }
    TINYPY_ASSERT(class_name_size - class_start <= SIZE_MAX - size - 1U);
    mangled_size = 1U + class_name_size - class_start + size;
    mangled = (uint8_t *)tinypy_internal_vm_allocate(vm, mangled_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    mangled[0] = '_';
    (void)memcpy(mangled + 1U, class_name + class_start, class_name_size - class_start);
    (void)memcpy(mangled + 1U + class_name_size - class_start, bytes, size);
    tinypy_value_t *result = tinypy_string_from_bytes(vm, mangled, mangled_size);
    tinypy_internal_vm_deallocate(vm, mangled, mangled_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
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
        return tinypy_tuple_from_items(vm, NULL, 0U);
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
        tinypy_list_append(names, name);
        TINYPY_DECREF(name);
    }
    size_t list_size = TINYPY_LIST_SIZE(names);
    tinypy_value_t **items = list_size != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, list_size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
    for (index = 0U; index < list_size; ++index) {
        items[index] = TINYPY_LIST_GET(names, index);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, list_size);
    if (items != NULL) {
        tinypy_internal_vm_deallocate(vm, items, list_size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    TINYPY_DECREF(names);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_type_t *__tinypy_internal_select_layout_base(tinypy_vm_t *vm, const tinypy_type_t *const *bases, size_t base_count, tinypy_error_t **out_error) {
    const tinypy_type_t *layout_base = bases[0];
    const tinypy_type_t *native_base = NULL;
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

        if (candidate->slot_count == 0U || candidate == layout_base) {
            continue;
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
tinypy_type_t *tinypy_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
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

    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    TINYPY_ASSERT(bases != NULL || base_count == 0U);
    if (namespace_dict != NULL) {
        TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, namespace_dict));
    }
    TINYPY_CLEAR_ERROR(out_error);

    if (actual_base_count == 0U) {
        default_base = &vm->object_type;
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
        TINYPY_ASSERT(TINYPY_VALUE_KIND(namespace_dict) == TINYPY_VALUE_DICT);
        TINYPY_INCREF(namespace_dict);
        dict = namespace_dict;
    }
    own_slots = __tinypy_internal_type_parse_slots(vm, name, name_size, dict, &slots_declared, &dict_slot, &weakref_slot, out_error);
    if (own_slots == NULL) {
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
        return NULL;
    }
    if (layout_base->layout_kind == TINYPY_VALUE_TUPLE && TINYPY_TUPLE_SIZE(own_slots) != 0U) {
        TINYPY_DECREF(own_slots);
        TINYPY_DECREF(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "nonempty __slots__ are not supported for tuple subtypes", out_error);
        return NULL;
    }
    instance_kind = tinypy_type_is_subtype(layout_base, &vm->type_type) != 0
                        ? TINYPY_VALUE_TYPE
                        : (layout_base->layout_kind != TINYPY_VALUE_INVALID ? layout_base->layout_kind : TINYPY_VALUE_INSTANCE);
    type = (tinypy_type_t *)tinypy_internal_object_allocate(
        vm, metaclass, sizeof(*type));

    type->vm = vm;
    inherited_slot_count = layout_base->slot_count;
    type->slot_count = inherited_slot_count + TINYPY_TUPLE_SIZE(own_slots);
    type->has_instance_dict = layout_base->has_instance_dict != 0 || slots_declared == 0 || dict_slot != 0 ? INT32_C(1) : INT32_C(0);
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
        type->dict_offset = layout_base->dict_offset;
        type->weakref_offset = layout_base->weakref_offset;
    }
    else {
        type->slots_offset = offsetof(tinypy_instance_object_t, slots);
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_instance_object_t, dict) : 0U;
        if (layout_base->weakref_offset != 0U || slots_declared == 0 || weakref_slot != 0) {
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
                                   : (instance_kind == TINYPY_VALUE_WEAKREF ? tinypy_internal_weakref_release_references : (instance_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_subclass_release_references : (instance_kind == TINYPY_VALUE_NATIVE_INSTANCE ? tinypy_internal_native_instance_release_references : tinypy_internal_instance_release_references)));
    type->destroy = instance_kind == TINYPY_VALUE_TYPE
                        ? tinypy_internal_type_destroy
                        : (instance_kind == TINYPY_VALUE_WEAKREF ? tinypy_internal_weakref_destroy : (instance_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_subclass_destroy : (instance_kind == TINYPY_VALUE_NATIVE_INSTANCE ? tinypy_internal_native_instance_destroy : NULL)));

    name_object = tinypy_string_from_bytes(vm, name, name_size);
    type->name = (const char *)TINYPY_STRING_OBJECT(name_object)->bytes;
    type->name_size = name_size;

    TINYPY_ASSERT(actual_base_count <= SIZE_MAX / sizeof(*base_values));
    base_values_size = actual_base_count * sizeof(*base_values);
    base_values = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, base_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    for (index = 0U; index < actual_base_count; ++index) {
        base_values[index] = &((tinypy_type_t *)actual_bases[index])->base.base;
    }
    bases_tuple = tinypy_tuple_from_items(
        vm, base_values, actual_base_count);

    TINYPY_ASSERT(mro_tail_count != SIZE_MAX);
    TINYPY_ASSERT(mro_tail_count + 1U <= SIZE_MAX / sizeof(*mro_values));
    mro_values_size = (mro_tail_count + 1U) * sizeof(*mro_values);
    mro_values = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, mro_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    mro_values[0] = &type->base.base;
    for (index = 0U; index < mro_tail_count; ++index) {
        mro_values[index + 1U] = &mro_types[index + 1U]->base.base;
    }
    mro_tuple = tinypy_internal_tuple_from_borrowed_items(
        vm, mro_values, mro_tail_count + 1U);

    type->name_object = name_object;
    type->dict = dict;
    TINYPY_DICT_OBJECT(type->dict)->type_dictionary = INT32_C(1);
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
            tinypy_internal_vm_deallocate(vm, mro_values, mro_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
            tinypy_internal_vm_deallocate(vm, base_values, base_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
            tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slot name conflicts with a class variable", out_error);
            return NULL;
        }
        descriptor = tinypy_internal_member_descriptor_new(type, slot_name, inherited_slot_count + slot_index);
        tinypy_dict_set(type->dict, slot_name, descriptor);
        TINYPY_DECREF(descriptor);
    }
    TINYPY_DECREF(own_slots);
    type->has_finalizer = tinypy_type_get_attr(type, "__del__", 7U) != NULL ? INT32_C(1) : INT32_C(0);

    tinypy_internal_vm_deallocate(
        vm, mro_values, mro_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    tinypy_internal_vm_deallocate(
        vm, base_values, base_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    tinypy_internal_vm_deallocate(
        vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    for (index = 0U; index < actual_base_count; ++index) {
        __tinypy_internal_type_add_subclass((tinypy_type_t *)actual_bases[index], type);
    }
    return type;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_lookup_cache_invalidate(tinypy_vm_t *vm) {
    TINYPY_ASSERT(vm->type_lookup_cache_epoch != UINT64_MAX);
    vm->type_lookup_cache_epoch += UINT64_C(1);
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
static inline int32_t __tinypy_internal_type_lookup_cacheable(const tinypy_vm_t *vm, const tinypy_value_t *key) {
    return key->type == &vm->string_type ? INT32_C(1) : INT32_C(0);
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
        if (entry->epoch == vm->type_lookup_cache_epoch && entry->type == type && entry->hash == hash && (entry->key == key || tinypy_internal_equal_value(entry->key, key, 1) != 0)) {
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
                entry->epoch = vm->type_lookup_cache_epoch;
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
        entry->epoch = vm->type_lookup_cache_epoch;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_type_get_attr(const tinypy_type_t *type, const char *name, size_t name_size) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    tinypy_vm_t *vm = type->vm;
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_internal_type_lookup_key(vm, type, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_type_set_attr(tinypy_type_t *type, const char *name, size_t name_size, tinypy_value_t *value) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    tinypy_vm_t *vm = type->vm;
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, value));
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_internal_type_set_attr_key(type, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_set_attr_key(tinypy_type_t *type, tinypy_value_t *key, tinypy_value_t *value) {
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(type->vm, key));
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(type->vm, value));
    TINYPY_ASSERT((type->flags & TINYPY_TYPE_FLAG_IMMUTABLE) == 0U);
    TINYPY_ASSERT(type->version_tag != UINT64_MAX);
    tinypy_dict_set(type->dict, key, value);
    type->version_tag += UINT64_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t **tinypy_internal_object_dict_slot(tinypy_value_t *value) {
    TINYPY_ASSERT(value != NULL);
    if (value->type->dict_offset == 0U) {
        return NULL;
    }
    return (tinypy_value_t **)((uint8_t *)value + value->type->dict_offset);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t **tinypy_internal_object_member_slot(tinypy_value_t *value, size_t index) {
    TINYPY_ASSERT(value != NULL);
    TINYPY_ASSERT(value->type->slots_offset != 0U);
    TINYPY_ASSERT(index < value->type->slot_count);
    return (tinypy_value_t **)((uint8_t *)value + value->type->slots_offset) + index;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

    if (dict_slot != NULL && *dict_slot != NULL) {
        visit(*dict_slot, user_data);
    } {
        size_t index;

        for (index = 0U; index < value->type->slot_count; ++index) {
            tinypy_value_t **slot = tinypy_internal_object_member_slot(value, index);

            if (*slot != NULL) {
                visit(*slot, user_data);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_instance_new(tinypy_type_t *type) {
    TINYPY_ASSERT(__tinypy_internal_type_valid(type));
    tinypy_vm_t *vm = type->vm;
    TINYPY_ASSERT((type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U);
    TINYPY_ASSERT((type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) == 0U);
    TINYPY_ASSERT(type->layout_kind == TINYPY_VALUE_INSTANCE);
    TINYPY_ASSERT(type->basic_size >= sizeof(tinypy_instance_object_t));

    return tinypy_internal_object_allocate(vm, type, type->basic_size);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_value_t *tinypy_instance_dict(const tinypy_value_t *instance) {
    TINYPY_ASSERT(instance != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(instance)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_INSTANCE || TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NATIVE_INSTANCE); {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot((tinypy_value_t *)instance);

        return dict_slot != NULL ? *dict_slot : NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_instance_get_attr(tinypy_value_t *instance_value, const char *name, size_t name_size) {
    tinypy_value_t *value;

    TINYPY_ASSERT(instance_value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    TINYPY_ASSERT(TINYPY_VALUE_KIND(instance_value) == TINYPY_VALUE_INSTANCE);
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

    TINYPY_ASSERT(instance_value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, value));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(instance_value) == TINYPY_VALUE_INSTANCE);
    TINYPY_ASSERT(instance_value->type->has_instance_dict != 0);
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance_value);
    TINYPY_ASSERT(dict_slot != NULL);
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
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    TINYPY_ASSERT(argument_count != SIZE_MAX);
    TINYPY_ASSERT(argument_count + 1U <= SIZE_MAX / sizeof(tinypy_value_t *));
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (argument_count + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    items[0] = first;
    for (size_t index = 0U; index < argument_count; ++index) {
        items[index + 1U] = TINYPY_TUPLE_GET(args, index);
    }
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, items, argument_count + 1U);
    tinypy_internal_vm_deallocate(vm, items, (argument_count + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_value_t *result = tinypy_call(callable, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_raw_callable(tinypy_value_t *attribute) {
    if (TINYPY_VALUE_KIND(attribute) == TINYPY_VALUE_STATIC_METHOD) {
        return tinypy_static_method_callable(attribute);
    }
    if (TINYPY_VALUE_KIND(attribute) == TINYPY_VALUE_CLASS_METHOD) {
        return tinypy_class_method_callable(attribute);
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
        return tinypy_internal_exception_instantiate(type, args, kwargs, out_error);
    }
    if (type->create != NULL) {
        return type->create(type, args, kwargs, out_error);
    }
    if ((type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin type has no public constructor", out_error);
        return NULL;
    }
    tinypy_value_t *new_attribute = tinypy_type_get_attr(type, "__new__", 7U);
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
        tinypy_value_t *metaclass_initializer = tinypy_type_get_attr(type, "__init__", 8U);

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
    initializer_attribute = tinypy_type_get_attr(type, "__init__", 8U);
    if (initializer_attribute == NULL) {
        tinypy_value_t *object_new = tinypy_type_get_attr(&vm->object_type, "__new__", 7U);

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
