#include "tinypy/type.h"

#include "internal.h"

#include <string.h>

typedef struct tinypy_mro_sequence_t {
    tinypy_type_t **items;
    size_t size;
    size_t position;
} tinypy_mro_sequence_t;

static void __tinypy_internal_type_error(
    tinypy_vm_t *vm,
    const char *message,
    tinypy_error_t **out_error)
{
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
}

#ifndef NDEBUG
static int __tinypy_internal_type_valid(
    const tinypy_type_t *type)
{
    return type != NULL &&
        tinypy_internal_vm_valid(type->vm) &&
        type->base.base.type != NULL &&
        (type->base.base.type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U;
}
#endif

static size_t __tinypy_internal_type_mro_size_raw(const tinypy_type_t *type)
{
    const tinypy_type_t *current;
    size_t count = 0U;

    if (type->mro != NULL) {
        return (size_t)TINYPY_SIZE(type->mro);
    }

    current = type;
    while (current != NULL) {
        count += 1U;
        current = current->base_type;
    }
    return count;
}

static tinypy_type_t *__tinypy_internal_type_mro_at_raw(
    const tinypy_type_t *type,
    size_t index)
{
    const tinypy_type_t *current;

    if (type->mro != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(type->mro);

        return (tinypy_type_t *)items[index];
    }

    current = type;
    while (index != 0U && current != NULL) {
        current = current->base_type;
        index -= 1U;
    }
    return (tinypy_type_t *)current;
}

static size_t __tinypy_internal_type_bases_size_raw(const tinypy_type_t *type)
{
    if (type->bases != NULL) {
        return (size_t)TINYPY_SIZE(type->bases);
    }
    return type->base_type != NULL ? 1U : 0U;
}

static tinypy_type_t *__tinypy_internal_type_base_at_raw(
    const tinypy_type_t *type,
    size_t index)
{
    if (type->bases != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(type->bases);

        return (tinypy_type_t *)items[index];
    }
    return index == 0U ? type->base_type : NULL;
}

static tinypy_value_t *__tinypy_internal_type_remove_subclass(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_type_t *base = (tinypy_type_t *)user_data;
    tinypy_value_t *reference;
    size_t size;
    size_t found = SIZE_MAX;
    size_t index;

    (void)function;
    (void)kwargs;
    tinypy_internal_clear_error(out_error);
    assert(kwargs == NULL || tinypy_dict_size(kwargs) == 0U);
    assert(tinypy_tuple_size(args) == 1U);
    assert(base != NULL);
    reference = tinypy_tuple_get(args, 0U);
    if (base->subclasses == NULL) return tinypy_none_get(base->vm);
    size = tinypy_tuple_size(base->subclasses);
    for (index = 0U; index < size; index += 1U) {
        if (tinypy_tuple_get(base->subclasses, index) == reference) {
            found = index;
            break;
        }
    }
    if (found != SIZE_MAX) {
        tinypy_value_t *previous = base->subclasses;

        if (size == 1U) {
            base->subclasses = NULL;
        } else {
            tinypy_value_t **items;
            size_t output_index = 0U;

            items = (tinypy_value_t **)tinypy_internal_vm_allocate(base->vm, (size - 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            for (index = 0U; index < size; index += 1U) {
                if (index != found) items[output_index++] = tinypy_tuple_get(previous, index);
            }
            base->subclasses = tinypy_tuple_from_items(base->vm, items, size - 1U);
            tinypy_internal_vm_deallocate(base->vm, items, (size - 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        }
        tinypy_release(previous);
    }
    return tinypy_none_get(base->vm);
}

static void __tinypy_internal_type_add_subclass(tinypy_type_t *base, tinypy_type_t *subclass)
{
    tinypy_error_t *error = NULL;
    tinypy_value_t *callback;
    tinypy_value_t *reference;
    tinypy_value_t **items;
    tinypy_value_t *subclasses;
    size_t size = base->subclasses != NULL ? tinypy_tuple_size(base->subclasses) : 0U;
    size_t index;

    callback = tinypy_native_function_new(base->vm, "__remove_subclass", 17U, __tinypy_internal_type_remove_subclass, base, NULL);
    reference = tinypy_weakref_new(&subclass->base.base, callback, &error);
    tinypy_release(callback);
    assert(reference != NULL);
    assert(error == NULL);
    assert(size < SIZE_MAX / sizeof(*items));
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(base->vm, (size + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < size; index += 1U) items[index] = tinypy_tuple_get(base->subclasses, index);
    items[size] = reference;
    subclasses = tinypy_tuple_from_items(base->vm, items, size + 1U);
    tinypy_internal_vm_deallocate(base->vm, items, (size + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (base->subclasses != NULL) tinypy_release(base->subclasses);
    base->subclasses = subclasses;
    tinypy_release(reference);
}

tinypy_value_t *tinypy_internal_type_subclasses(tinypy_type_t *type)
{
    tinypy_value_t *result = tinypy_list_from_items(type->vm, NULL, 0U);
    size_t index;

    if (type->subclasses == NULL) return result;
    for (index = 0U; index < tinypy_tuple_size(type->subclasses); index += 1U) {
        tinypy_value_t *subclass = tinypy_weakref_get(tinypy_tuple_get(type->subclasses, index));

        if (subclass != NULL) tinypy_list_append(result, subclass);
    }
    return result;
}

int tinypy_type_is_subtype(
    const tinypy_type_t *type,
    const tinypy_type_t *candidate_base)
{
    size_t count;
    size_t index;

    assert(__tinypy_internal_type_valid(type));
    assert(__tinypy_internal_type_valid(candidate_base));
    assert(type->vm == candidate_base->vm);

    count = __tinypy_internal_type_mro_size_raw(type);
    for (index = 0U; index < count; index += 1U) {
        if (__tinypy_internal_type_mro_at_raw(type, index) == candidate_base) {
            return 1;
        }
    }
    return 0;
}

tinypy_value_t *tinypy_type_as_value(tinypy_type_t *type)
{
    assert(__tinypy_internal_type_valid(type));
    return &type->base.base;
}

const tinypy_value_t *tinypy_type_as_const_value(const tinypy_type_t *type)
{
    assert(__tinypy_internal_type_valid(type));
    return &type->base.base;
}

tinypy_type_t *tinypy_value_as_type(tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_TYPE);
    return (tinypy_type_t *)value;
}

const tinypy_type_t *tinypy_value_as_const_type(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_TYPE);
    return (const tinypy_type_t *)value;
}

size_t tinypy_type_bases_size(const tinypy_type_t *type)
{
    assert(__tinypy_internal_type_valid(type));
    return __tinypy_internal_type_bases_size_raw(type);
}

const tinypy_type_t *tinypy_type_base_at(
    const tinypy_type_t *type,
    size_t index)
{
    assert(__tinypy_internal_type_valid(type));
    assert(index < __tinypy_internal_type_bases_size_raw(type));
    return __tinypy_internal_type_base_at_raw(type, index);
}

size_t tinypy_type_mro_size(const tinypy_type_t *type)
{
    assert(__tinypy_internal_type_valid(type));
    return __tinypy_internal_type_mro_size_raw(type);
}

const tinypy_type_t *tinypy_type_mro_at(
    const tinypy_type_t *type,
    size_t index)
{
    assert(__tinypy_internal_type_valid(type));
    assert(index < __tinypy_internal_type_mro_size_raw(type));
    return __tinypy_internal_type_mro_at_raw(type, index);
}

static tinypy_type_t *__tinypy_internal_select_metaclass(
    tinypy_vm_t *vm,
    const tinypy_type_t *const *bases,
    size_t base_count,
    const tinypy_type_t *explicit_metaclass,
    tinypy_error_t **out_error)
{
    tinypy_type_t *winner;
    size_t index;

    if (explicit_metaclass != NULL) {
        assert(__tinypy_internal_type_valid(explicit_metaclass));
        assert(explicit_metaclass->vm == vm);
        if (!tinypy_type_is_subtype(explicit_metaclass, &vm->type_type)) {
            __tinypy_internal_type_error(
                vm,
                "explicit metaclass is not a subtype of type", out_error);
            return NULL;
        }
        winner = (tinypy_type_t *)explicit_metaclass;
    } else {
        winner = bases[0]->base.base.type;
    }

    for (index = 0U; index < base_count; index += 1U) {
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

static int __tinypy_internal_mro_head_in_tail(
    tinypy_type_t *candidate,
    const tinypy_mro_sequence_t *sequences,
    size_t sequence_count)
{
    size_t sequence_index;

    for (sequence_index = 0U;
         sequence_index < sequence_count;
         sequence_index += 1U) {
        const tinypy_mro_sequence_t *sequence = &sequences[sequence_index];
        size_t item_index;

        for (item_index = sequence->position + 1U;
             item_index < sequence->size;
             item_index += 1U) {
            if (sequence->items[item_index] == candidate) {
                return 1;
            }
        }
    }
    return 0;
}

static void __tinypy_internal_mro_free(
    tinypy_vm_t *vm,
    tinypy_mro_sequence_t *sequences,
    size_t sequence_size,
    tinypy_type_t **storage,
    size_t storage_size,
    tinypy_type_t **result,
    size_t result_size)
{
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

static tinypy_type_t **__tinypy_internal_c3_merge(
    tinypy_vm_t *vm,
    const tinypy_type_t *const *bases,
    size_t base_count,
    size_t *out_count,
    size_t *out_allocation_size,
    tinypy_error_t **out_error)
{
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

    assert(base_count != SIZE_MAX);
    sequence_count = base_count + 1U;
    assert(sequence_count <= SIZE_MAX / sizeof(*sequences));
    sequence_size = sequence_count * sizeof(*sequences);

    for (index = 0U; index < base_count; index += 1U) {
        size_t base_mro_size = __tinypy_internal_type_mro_size_raw(bases[index]);

        assert(total_items <= SIZE_MAX - base_mro_size);
        total_items += base_mro_size;
    }
    assert(total_items <= SIZE_MAX / sizeof(*storage));
    assert(total_items != SIZE_MAX);
    storage_size = total_items * sizeof(*storage);
    result_capacity = total_items + 1U;
    assert(result_capacity <= SIZE_MAX / sizeof(*result));
    result_size = result_capacity * sizeof(*result);

    sequences = (tinypy_mro_sequence_t *)tinypy_internal_vm_allocate(
        vm, sequence_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    storage = (tinypy_type_t **)tinypy_internal_vm_allocate(
        vm, storage_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    result = (tinypy_type_t **)tinypy_internal_vm_allocate(
        vm, result_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    (void)memset(sequences, 0, sequence_size);
    result[0] = NULL;

    for (index = 0U; index < base_count; index += 1U) {
        tinypy_mro_sequence_t *sequence = &sequences[index];
        size_t mro_index;

        sequence->items = &storage[storage_offset];
        sequence->size = __tinypy_internal_type_mro_size_raw(bases[index]);
        for (mro_index = 0U;
             mro_index < sequence->size;
             mro_index += 1U) {
            sequence->items[mro_index] =
                __tinypy_internal_type_mro_at_raw(bases[index], mro_index);
        }
        storage_offset += sequence->size;
    }
    sequences[base_count].items = &storage[storage_offset];
    sequences[base_count].size = base_count;
    for (index = 0U; index < base_count; index += 1U) {
        sequences[base_count].items[index] = (tinypy_type_t *)bases[index];
    }

    for (;;) {
        tinypy_type_t *candidate = NULL;
        int has_items = 0;
        size_t sequence_index;

        for (sequence_index = 0U;
             sequence_index < sequence_count;
             sequence_index += 1U) {
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
             sequence_index += 1U) {
            tinypy_mro_sequence_t *sequence = &sequences[sequence_index];

            if (sequence->position < sequence->size &&
                sequence->items[sequence->position] == candidate) {
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

static int32_t __tinypy_internal_validate_bases(
    tinypy_vm_t *vm,
    const tinypy_type_t *const *bases,
    size_t base_count,
    tinypy_error_t **out_error)
{
    size_t index;

    for (index = 0U; index < base_count; index += 1U) {
        size_t earlier;

        assert(__tinypy_internal_type_valid(bases[index]));
        assert(bases[index]->vm == vm);
        if ((bases[index]->flags & TINYPY_TYPE_FLAG_BASE_TYPE) == 0U) {
            __tinypy_internal_type_error(
                vm,
                "selected type does not permit subclassing", out_error);
            return INT32_C(0);
        }
        for (earlier = 0U; earlier < index; earlier += 1U) {
            if (bases[earlier] == bases[index]) {
                __tinypy_internal_type_error(
                    vm, "duplicate type base", out_error);
                return INT32_C(0);
            }
        }
    }
    return INT32_C(1);
}

static void __tinypy_internal_release_if_not_null(tinypy_value_t *value)
{
    if (value != NULL) {
        tinypy_release(value);
    }
}

static tinypy_value_t *__tinypy_internal_type_namespace_value(tinypy_vm_t *vm, tinypy_value_t *namespace_dict, const char *name, size_t name_size)
{
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_dict_contains(namespace_dict, key) != 0 ? tinypy_dict_get(namespace_dict, key) : NULL;

    tinypy_release(key);
    return value;
}

static int32_t __tinypy_internal_type_slot_name_equal(tinypy_value_t *value, const char *name, size_t name_size)
{
    const unsigned char *bytes = tinypy_internal_text_bytes(value);
    size_t size = tinypy_internal_text_byte_size(value);

    return size == name_size && (size == 0U || memcmp(bytes, name, size) == 0) ? INT32_C(1) : INT32_C(0);
}

static tinypy_value_t *__tinypy_internal_type_mangle_slot_name(tinypy_vm_t *vm, const char *class_name, size_t class_name_size, tinypy_value_t *slot_name)
{
    const unsigned char *bytes = tinypy_internal_text_bytes(slot_name);
    size_t size = tinypy_internal_text_byte_size(slot_name);
    size_t class_start = 0U;
    unsigned char *mangled;
    size_t mangled_size;
    tinypy_value_t *result;

    if (size < 3U || bytes[0] != '_' || bytes[1] != '_' || (bytes[size - 2U] == '_' && bytes[size - 1U] == '_')) return tinypy_string_from_bytes(vm, bytes, size);
    while (class_start < class_name_size && class_name[class_start] == '_') class_start += 1U;
    if (class_start == class_name_size) return tinypy_string_from_bytes(vm, bytes, size);
    assert(class_name_size - class_start <= SIZE_MAX - size - 1U);
    mangled_size = 1U + class_name_size - class_start + size;
    mangled = (unsigned char *)tinypy_internal_vm_allocate(vm, mangled_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    mangled[0] = '_';
    (void)memcpy(mangled + 1U, class_name + class_start, class_name_size - class_start);
    (void)memcpy(mangled + 1U + class_name_size - class_start, bytes, size);
    result = tinypy_string_from_bytes(vm, mangled, mangled_size);
    tinypy_internal_vm_deallocate(vm, mangled, mangled_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

static tinypy_value_t *__tinypy_internal_type_parse_slots(tinypy_vm_t *vm, const char *class_name, size_t class_name_size, tinypy_value_t *namespace_dict, int32_t *out_declared, int32_t *out_dict, int32_t *out_weakref, tinypy_error_t **out_error)
{
    tinypy_value_t *declaration = __tinypy_internal_type_namespace_value(vm, namespace_dict, "__slots__", 9U);
    tinypy_value_t *names = tinypy_list_from_items(vm, NULL, 0U);
    size_t input_size = 0U;
    size_t index;
    tinypy_value_t **items;
    tinypy_value_t *result;

    *out_declared = declaration != NULL ? INT32_C(1) : INT32_C(0);
    *out_dict = INT32_C(0);
    *out_weakref = INT32_C(0);
    if (declaration == NULL) {
        tinypy_release(names);
        return tinypy_tuple_from_items(vm, NULL, 0U);
    }
    if (tinypy_internal_value_kind(declaration) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(declaration) == TINYPY_VALUE_UNICODE) input_size = 1U;
    else if (tinypy_internal_value_kind(declaration) == TINYPY_VALUE_TUPLE) input_size = tinypy_tuple_size(declaration);
    else if (tinypy_internal_value_kind(declaration) == TINYPY_VALUE_LIST) input_size = tinypy_list_size(declaration);
    else {
        tinypy_release(names);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__slots__ must be a string or a sequence of strings", out_error);
        return NULL;
    }
    for (index = 0U; index < input_size; index += 1U) {
        tinypy_value_t *source = input_size == 1U && (tinypy_internal_value_kind(declaration) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(declaration) == TINYPY_VALUE_UNICODE)
            ? declaration
            : (tinypy_internal_value_kind(declaration) == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(declaration, index) : tinypy_list_get(declaration, index));
        tinypy_value_t *name;
        size_t existing;

        if (tinypy_internal_value_kind(source) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(source) != TINYPY_VALUE_UNICODE) {
            tinypy_release(names);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__slots__ entries must be strings", out_error);
            return NULL;
        }
        if (__tinypy_internal_type_slot_name_equal(source, "__dict__", 8U) != 0) {
            if (*out_dict != 0) {
                tinypy_release(names);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__dict__ slot is duplicated", out_error);
                return NULL;
            }
            *out_dict = INT32_C(1);
            continue;
        }
        if (__tinypy_internal_type_slot_name_equal(source, "__weakref__", 11U) != 0) {
            if (*out_weakref != 0) {
                tinypy_release(names);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__weakref__ slot is duplicated", out_error);
                return NULL;
            }
            *out_weakref = INT32_C(1);
            continue;
        }
        name = __tinypy_internal_type_mangle_slot_name(vm, class_name, class_name_size, source);
        for (existing = 0U; existing < tinypy_list_size(names); existing += 1U) {
            if (tinypy_internal_equal_value(tinypy_list_get(names, existing), name, 1) != 0) {
                tinypy_release(name);
                tinypy_release(names);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__slots__ entry is duplicated", out_error);
                return NULL;
            }
        }
        tinypy_list_append(names, name);
        tinypy_release(name);
    }
    items = tinypy_list_size(names) != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, tinypy_list_size(names) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
    for (index = 0U; index < tinypy_list_size(names); index += 1U) items[index] = tinypy_list_get(names, index);
    result = tinypy_tuple_from_items(vm, items, tinypy_list_size(names));
    if (items != NULL) tinypy_internal_vm_deallocate(vm, items, tinypy_list_size(names) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_release(names);
    return result;
}

tinypy_type_t *tinypy_type_new(
    tinypy_vm_t *vm,
    const char *name,
    size_t name_size,
    const tinypy_type_t *const *bases,
    size_t base_count,
    const tinypy_type_t *explicit_metaclass,
    tinypy_value_t *namespace_dict,
    tinypy_error_t **out_error)
{
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

    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(bases != NULL || base_count == 0U);
    if (namespace_dict != NULL) {
        assert(tinypy_internal_value_belongs_to(vm, namespace_dict));
    }
    tinypy_internal_clear_error(out_error);

    if (actual_base_count == 0U) {
        default_base = &vm->object_type;
        actual_bases = &default_base;
        actual_base_count = 1U;
    }
    if (__tinypy_internal_validate_bases(
            vm, actual_bases, actual_base_count, out_error) == 0) {
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
    } else {
        assert(tinypy_internal_value_kind(namespace_dict) == TINYPY_VALUE_DICT);
        tinypy_retain(namespace_dict);
        dict = namespace_dict;
    }
    own_slots = __tinypy_internal_type_parse_slots(vm, name, name_size, dict, &slots_declared, &dict_slot, &weakref_slot, out_error);
    if (own_slots == NULL) {
        tinypy_release(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
        return NULL;
    }
    if (actual_bases[0]->layout_kind == TINYPY_VALUE_TUPLE && tinypy_tuple_size(own_slots) != 0U) {
        tinypy_release(own_slots);
        tinypy_release(dict);
        tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "nonempty __slots__ are not supported for tuple subtypes", out_error);
        return NULL;
    }
    instance_kind = tinypy_type_is_subtype(actual_bases[0], &vm->type_type) != 0
        ? TINYPY_VALUE_TYPE
        : (actual_bases[0]->layout_kind != TINYPY_VALUE_INVALID ? actual_bases[0]->layout_kind : TINYPY_VALUE_INSTANCE);
    type = (tinypy_type_t *)tinypy_internal_object_allocate(
        vm, metaclass, sizeof(*type));

    type->vm = vm;
    inherited_slot_count = actual_bases[0]->slot_count;
    type->slot_count = inherited_slot_count + tinypy_tuple_size(own_slots);
    type->has_instance_dict = actual_bases[0]->has_instance_dict != 0 || slots_declared == 0 || dict_slot != 0 ? INT32_C(1) : INT32_C(0);
    type->layout_kind = instance_kind;
    if (instance_kind == TINYPY_VALUE_TYPE) {
        type->basic_size = sizeof(tinypy_type_t);
        type->dict_offset = 0U;
        type->slots_offset = 0U;
    } else if (instance_kind == TINYPY_VALUE_WEAKREF) {
        type->slots_offset = offsetof(tinypy_weakref_object_t, slots);
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_weakref_object_t, dict) : 0U;
    } else if (instance_kind == TINYPY_VALUE_TUPLE) {
        type->slots_offset = 0U;
        type->slot_count = 0U;
        type->basic_size = sizeof(tinypy_tuple_subclass_object_t);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_tuple_subclass_object_t, dict) : 0U;
    } else {
        type->slots_offset = offsetof(tinypy_instance_object_t, slots);
        type->basic_size = type->slots_offset + type->slot_count * sizeof(tinypy_value_t *);
        type->dict_offset = type->has_instance_dict != 0 ? offsetof(tinypy_instance_object_t, dict) : 0U;
        if (actual_bases[0]->weakref_offset != 0U || slots_declared == 0 || weakref_slot != 0) {
            type->weakref_offset = type->basic_size;
            type->basic_size += sizeof(tinypy_value_t *);
        }
    }
    type->flags = TINYPY_TYPE_FLAG_HEAP |
        TINYPY_TYPE_FLAG_BASE_TYPE;
    if (instance_kind == TINYPY_VALUE_TYPE) {
        type->flags |= TINYPY_TYPE_FLAG_TYPE_SUBCLASS;
    }
    type->base_type = (tinypy_type_t *)actual_bases[0];
    type->number_slots = actual_bases[0]->number_slots;
    type->sequence_slots = actual_bases[0]->sequence_slots;
    type->mapping_slots = actual_bases[0]->mapping_slots;
    type->repr = actual_bases[0]->repr;
    type->string = actual_bases[0]->string;
    type->hash = actual_bases[0]->hash;
    type->call = actual_bases[0]->call;
    type->get_attribute = actual_bases[0]->get_attribute;
    type->set_attribute = actual_bases[0]->set_attribute;
    type->rich_compare = actual_bases[0]->rich_compare;
    type->iter = actual_bases[0]->iter;
    type->next = actual_bases[0]->next;
    type->descriptor_get = actual_bases[0]->descriptor_get;
    type->descriptor_set = actual_bases[0]->descriptor_set;
    type->release_references = instance_kind == TINYPY_VALUE_TYPE
        ? tinypy_internal_type_release_references
        : (instance_kind == TINYPY_VALUE_WEAKREF ? tinypy_internal_weakref_release_references : (instance_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_subclass_release_references : tinypy_internal_instance_release_references));
    type->destroy = instance_kind == TINYPY_VALUE_TYPE
        ? tinypy_internal_type_destroy
        : (instance_kind == TINYPY_VALUE_WEAKREF ? tinypy_internal_weakref_destroy : (instance_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_subclass_destroy : NULL));

    name_object = tinypy_string_from_bytes(vm, name, name_size);
    type->name = (const char *)TINYPY_STRING_OBJECT(name_object)->bytes;
    type->name_size = name_size;

    assert(actual_base_count <= SIZE_MAX / sizeof(*base_values));
    base_values_size = actual_base_count * sizeof(*base_values);
    base_values = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, base_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    for (index = 0U; index < actual_base_count; index += 1U) {
        base_values[index] = &((tinypy_type_t *)actual_bases[index])->base.base;
    }
    bases_tuple = tinypy_tuple_from_items(
        vm, base_values, actual_base_count);

    assert(mro_tail_count != SIZE_MAX);
    assert(mro_tail_count + 1U <= SIZE_MAX / sizeof(*mro_values));
    mro_values_size = (mro_tail_count + 1U) * sizeof(*mro_values);
    mro_values = (tinypy_value_t **)tinypy_internal_vm_allocate(
        vm, mro_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    mro_values[0] = &type->base.base;
    for (index = 0U; index < mro_tail_count; index += 1U) {
        mro_values[index + 1U] = &mro_types[index + 1U]->base.base;
    }
    mro_tuple = tinypy_internal_tuple_from_borrowed_items(
        vm, mro_values, mro_tail_count + 1U);

    type->name_object = name_object;
    type->dict = dict;
    type->bases = bases_tuple;
    type->mro = mro_tuple;
    name_object = NULL;
    dict = NULL;
    bases_tuple = NULL;
    mro_tuple = NULL;

    for (index = 0U; index < tinypy_tuple_size(own_slots); index += 1U) {
        tinypy_value_t *slot_name = tinypy_tuple_get(own_slots, index);
        tinypy_value_t *descriptor;

        if (__tinypy_internal_type_namespace_value(vm, type->dict, (const char *)tinypy_internal_text_bytes(slot_name), tinypy_internal_text_byte_size(slot_name)) != NULL) {
            tinypy_release(own_slots);
            tinypy_release(&type->base.base);
            tinypy_internal_vm_deallocate(vm, mro_values, mro_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
            tinypy_internal_vm_deallocate(vm, base_values, base_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
            tinypy_internal_vm_deallocate(vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slot name conflicts with a class variable", out_error);
            return NULL;
        }
        descriptor = tinypy_internal_member_descriptor_new(type, slot_name, inherited_slot_count + index);
        tinypy_dict_set(type->dict, slot_name, descriptor);
        tinypy_release(descriptor);
    }
    tinypy_release(own_slots);
    type->has_finalizer = tinypy_type_get_attr(type, "__del__", 7U) != NULL ? INT32_C(1) : INT32_C(0);

    tinypy_internal_vm_deallocate(
        vm, mro_values, mro_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    tinypy_internal_vm_deallocate(
        vm, base_values, base_values_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    tinypy_internal_vm_deallocate(
        vm, mro_types, mro_workspace_size, (uint32_t)TINYPY_ALLOC_TAG_TYPE_MRO);
    for (index = 0U; index < actual_base_count; index += 1U) {
        __tinypy_internal_type_add_subclass((tinypy_type_t *)actual_bases[index], type);
    }
    return type;
}

static tinypy_value_t *__tinypy_internal_type_lookup_key(
    const tinypy_type_t *type,
    tinypy_value_t *key)
{
    size_t mro_size = __tinypy_internal_type_mro_size_raw(type);
    size_t index;

    for (index = 0U; index < mro_size; index += 1U) {
        tinypy_type_t *mro_type = __tinypy_internal_type_mro_at_raw(type, index);

        if (tinypy_dict_contains(mro_type->dict, key) != 0) {
            return tinypy_dict_get(mro_type->dict, key);
        }
    }
    return NULL;
}

tinypy_value_t *tinypy_type_get_attr(
    const tinypy_type_t *type,
    const char *name,
    size_t name_size)
{
    tinypy_vm_t *vm;
    tinypy_value_t *key;
    tinypy_value_t *value;

    assert(__tinypy_internal_type_valid(type));
    vm = type->vm;
    assert(name != NULL || name_size == 0U);
    key = tinypy_string_from_bytes(vm, name, name_size);
    value = __tinypy_internal_type_lookup_key(type, key);
    tinypy_release(key);
    return value;
}

void tinypy_type_set_attr(
    tinypy_type_t *type,
    const char *name,
    size_t name_size,
    tinypy_value_t *value)
{
    tinypy_vm_t *vm;
    tinypy_value_t *key = NULL;

    assert(__tinypy_internal_type_valid(type));
    vm = type->vm;
    assert(name != NULL || name_size == 0U);
    assert(tinypy_internal_value_belongs_to(vm, value));
    assert((type->flags & TINYPY_TYPE_FLAG_IMMUTABLE) == 0U);
    assert(type->version_tag != UINT64_MAX);
    key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_dict_set(type->dict, key, value);
    type->version_tag += UINT64_C(1);
    __tinypy_internal_release_if_not_null(key);
}

tinypy_value_t **tinypy_internal_object_dict_slot(tinypy_value_t *value)
{
    assert(value != NULL);
    if (value->type->dict_offset == 0U) return NULL;
    return (tinypy_value_t **)((unsigned char *)value + value->type->dict_offset);
}

tinypy_value_t **tinypy_internal_object_member_slot(tinypy_value_t *value, size_t index)
{
    assert(value != NULL);
    assert(value->type->slots_offset != 0U);
    assert(index < value->type->slot_count);
    return (tinypy_value_t **)((unsigned char *)value + value->type->slots_offset) + index;
}

void tinypy_internal_instance_release_references(
    tinypy_value_t *value,
    tinypy_release_callback_t visit,
    void *user_data)
{
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

    if (dict_slot != NULL && *dict_slot != NULL) visit(*dict_slot, user_data);
    {
        size_t index;

        for (index = 0U; index < value->type->slot_count; index += 1U) {
            tinypy_value_t **slot = tinypy_internal_object_member_slot(value, index);

            if (*slot != NULL) visit(*slot, user_data);
        }
    }
}

static tinypy_instance_object_t *__tinypy_internal_instance_validate(
    tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_INSTANCE);
    return (tinypy_instance_object_t *)value;
}

tinypy_value_t *tinypy_instance_new(tinypy_type_t *type)
{
    tinypy_vm_t *vm;

    assert(__tinypy_internal_type_valid(type));
    vm = type->vm;
    assert((type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U);
    assert((type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) == 0U);
    assert(type->layout_kind == TINYPY_VALUE_INSTANCE);
    assert(type->basic_size >= sizeof(tinypy_instance_object_t));

    return tinypy_internal_object_allocate(vm, type, type->basic_size);
}

const tinypy_value_t *tinypy_instance_dict(
    const tinypy_value_t *instance)
{
    assert(instance != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(instance)));
    assert(tinypy_internal_value_kind(instance) == TINYPY_VALUE_INSTANCE);
    {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot((tinypy_value_t *)instance);

        return dict_slot != NULL ? *dict_slot : NULL;
    }
}

tinypy_value_t *tinypy_instance_get_attr(
    tinypy_value_t *instance_value,
    const char *name,
    size_t name_size)
{
    tinypy_vm_t *vm;
    tinypy_value_t **dict_slot;
    tinypy_value_t *key;
    tinypy_value_t *value;

    assert(instance_value != NULL);
    vm = tinypy_internal_value_vm(instance_value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    (void)__tinypy_internal_instance_validate(instance_value);
    dict_slot = tinypy_internal_object_dict_slot(instance_value);

    key = tinypy_string_from_bytes(vm, name, name_size);
    if (dict_slot != NULL && *dict_slot != NULL) {
        if (tinypy_dict_contains(*dict_slot, key) != 0) {
            value = tinypy_dict_get(*dict_slot, key);
            tinypy_release(key);
            return value;
        }
    }
    value = __tinypy_internal_type_lookup_key(instance_value->type, key);
    tinypy_release(key);
    return value;
}

void tinypy_instance_set_attr(
    tinypy_value_t *instance_value,
    const char *name,
    size_t name_size,
    tinypy_value_t *value)
{
    tinypy_vm_t *vm;
    tinypy_value_t **dict_slot;
    tinypy_value_t *key = NULL;

    assert(instance_value != NULL);
    vm = tinypy_internal_value_vm(instance_value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(tinypy_internal_value_belongs_to(vm, value));
    (void)__tinypy_internal_instance_validate(instance_value);
    assert(instance_value->type->has_instance_dict != 0);
    dict_slot = tinypy_internal_object_dict_slot(instance_value);
    assert(dict_slot != NULL);
    if (*dict_slot == NULL) *dict_slot = tinypy_dict_new(vm);
    key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_dict_set(*dict_slot, key, value);
    __tinypy_internal_release_if_not_null(key);
}

static tinypy_value_t *__tinypy_internal_type_call_with_first(tinypy_value_t *callable, tinypy_value_t *first, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(callable);
    size_t argument_count = tinypy_tuple_size(args);
    tinypy_value_t **items;
    tinypy_value_t *call_args;
    tinypy_value_t *result;
    size_t index;

    assert(argument_count != SIZE_MAX);
    assert(argument_count + 1U <= SIZE_MAX / sizeof(*items));
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (argument_count + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    items[0] = first;
    for (index = 0U; index < argument_count; index += 1U) items[index + 1U] = tinypy_tuple_get(args, index);
    call_args = tinypy_tuple_from_items(vm, items, argument_count + 1U);
    tinypy_internal_vm_deallocate(vm, items, (argument_count + 1U) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    result = tinypy_call(callable, call_args, kwargs, out_error);
    tinypy_release(call_args);
    return result;
}

static tinypy_value_t *__tinypy_internal_type_raw_callable(tinypy_value_t *attribute)
{
    if (tinypy_internal_value_kind(attribute) == TINYPY_VALUE_STATIC_METHOD) return tinypy_static_method_callable(attribute);
    if (tinypy_internal_value_kind(attribute) == TINYPY_VALUE_CLASS_METHOD) return tinypy_class_method_callable(attribute);
    return attribute;
}

tinypy_value_t *tinypy_internal_type_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_type_t *type = (tinypy_type_t *)callable;
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *instance;
    tinypy_value_t *new_attribute;
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
    new_attribute = tinypy_type_get_attr(type, "__new__", 7U);
    if (new_attribute == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class has no __new__", out_error);
        return NULL;
    }
    instance = __tinypy_internal_type_call_with_first(__tinypy_internal_type_raw_callable(new_attribute), &type->base.base, args, kwargs, out_error);
    if (instance == NULL) return NULL;
    if ((type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U) {
        tinypy_value_t *metaclass_initializer = tinypy_type_get_attr(type, "__init__", 8U);

        if (metaclass_initializer != NULL) {
            tinypy_value_t *metaclass_initialize_result = __tinypy_internal_type_call_with_first(__tinypy_internal_type_raw_callable(metaclass_initializer), instance, args, kwargs, out_error);

            if (metaclass_initialize_result == NULL) {
                tinypy_release(instance);
                return NULL;
            }
            if (tinypy_internal_value_kind(metaclass_initialize_result) != TINYPY_VALUE_NONE) {
                tinypy_release(metaclass_initialize_result);
                tinypy_release(instance);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "metaclass __init__ must return None", out_error);
                return NULL;
            }
            tinypy_release(metaclass_initialize_result);
        }
        return instance;
    }
    if (tinypy_type_is_subtype(instance->type, type) == 0) return instance;
    initializer_attribute = tinypy_type_get_attr(type, "__init__", 8U);
    if (initializer_attribute == NULL) {
        tinypy_value_t *object_new = tinypy_type_get_attr(&vm->object_type, "__new__", 7U);

        if (new_attribute != object_new) return instance;
        if (tinypy_tuple_size(args) != 0U || (kwargs != NULL && tinypy_dict_size(kwargs) != 0U)) {
            tinypy_release(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class constructor takes no arguments", out_error);
            return NULL;
        }
        return instance;
    }
    initializer = tinypy_object_get_attr(instance, "__init__", 8U, out_error);
    if (initializer == NULL) {
        tinypy_release(instance);
        return NULL;
    }
    initialize_result = tinypy_call(initializer, args, kwargs, out_error);
    tinypy_release(initializer);
    if (initialize_result == NULL) {
        tinypy_release(instance);
        return NULL;
    }
    if (tinypy_internal_value_kind(initialize_result) != TINYPY_VALUE_NONE) {
        tinypy_release(initialize_result);
        tinypy_release(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__init__ must return None", out_error);
        return NULL;
    }
    tinypy_release(initialize_result);
    return instance;
}
