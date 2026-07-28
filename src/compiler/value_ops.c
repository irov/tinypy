#include "internal.h"

#include "value_ops.h"
#include "ast_sequence.h"
#include "bytecode_builder.h"

//////////////////////////////////////////////////////////////////////////
size_t __tinypy_frontend_string_size(const tinypy_value_t *value) {
    size_t size;

    (void)tinypy_string_view(value, &size);
    return size;
}
//////////////////////////////////////////////////////////////////////////
const void *__tinypy_frontend_string_data(const tinypy_value_t *value) {
    size_t size;

    const void *return_value_1 = tinypy_string_view(value, &size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_dict_set(dict, key, value);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_dict_get(tinypy_value_t *dict, tinypy_value_t *key) {
    tinypy_value_t *return_value_1 = tinypy_dict_get_optional(dict, key);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_dict_delete(tinypy_value_t *dict, tinypy_value_t *key) {
    if (tinypy_dict_contains(dict, key) == 0) {
        return -1;
    }
    tinypy_dict_delete(dict, key);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_dict_update(tinypy_value_t *dict, tinypy_value_t *source) {
    tinypy_compiler_size_t position = 0;
    tinypy_value_t *key;
    tinypy_value_t *value;

    while (__tinypy_frontend_dict_next(source, &position, &key, &value) != 0) {
        tinypy_dict_set(dict, key, value);
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t __tinypy_frontend_dict_next(tinypy_value_t *dict, tinypy_compiler_size_t *position, tinypy_value_t **key, tinypy_value_t **value) {
    size_t capacity;
    size_t index;

    capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
    index = (size_t)*position;
    while (index < capacity) {
        tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[index];

        index += 1U;
        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        *position = (tinypy_compiler_size_t)index;
        *key = entry->key;
        *value = entry->value;
        return TINYPY_TRUE;
    }
    *position = (tinypy_compiler_size_t)capacity;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_list_append(tinypy_value_t *list, tinypy_value_t *value) {
    tinypy_list_append(list, value);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_list_delete(tinypy_value_t *list, tinypy_compiler_size_t index) {
    tinypy_list_delete(list, (size_t)index);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_dict_new_from_owner(tinypy_value_t *owner) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    tinypy_value_t *return_value_1 = tinypy_dict_new(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_integer_from_owner(tinypy_value_t *owner, int64_t value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_string_from_owner(tinypy_value_t *owner, const char *bytes, size_t size) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, bytes, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_dict_set_none(tinypy_value_t *dict, tinypy_value_t *key) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_dict_set(dict, key, none);
    TINYPY_DECREF(none);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_format_identifier(tinypy_value_t *owner, const char *prefix, int32_t value, const char *suffix) {
    char reversed[32];
    char buffer[96];
    size_t prefix_size;
    size_t suffix_size;
    size_t digit_count = 0U;
    size_t index;
    uint32_t magnitude;
    size_t output_size;

    prefix_size = strlen(prefix);
    suffix_size = strlen(suffix);
    magnitude = value < 0 ? (uint32_t)(-(value + 1)) + 1U : (uint32_t)value;
    do {
        reversed[digit_count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    output_size = prefix_size + digit_count + suffix_size + (value < 0 ? 1U : 0U);
    (void)memcpy(buffer, prefix, prefix_size);
    index = prefix_size;
    if (value < 0) {
        buffer[index++] = '-';
    }
    while (digit_count != 0U) {
        buffer[index++] = reversed[--digit_count];
    }
    (void)memcpy(buffer + index, suffix, suffix_size);
    tinypy_value_t *return_value_1 = __tinypy_frontend_string_from_owner(owner, buffer, output_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_mangle(tinypy_compile_ctx_t *arena, tinypy_value_t *private_name, tinypy_value_t *identifier) {
    const char *name;
    const char *class_name;
    size_t name_size;
    size_t class_size;
    char *buffer;

    name = (const char *)__tinypy_frontend_string_data(identifier);
    name_size = __tinypy_frontend_string_size(identifier);
    if (private_name == NULL || name_size < 2U || name[0] != '_' || name[1] != '_' || (name[name_size - 1U] == '_' && name[name_size - 2U] == '_') || memchr(name, '.', name_size) != NULL) {
        TINYPY_INCREF(identifier);
        return identifier;
    }
    class_name = (const char *)__tinypy_frontend_string_data(private_name);
    class_size = __tinypy_frontend_string_size(private_name);
    while (class_size != 0U && *class_name == '_') {
        class_name += 1;
        class_size -= 1U;
    }
    if (class_size == 0U) {
        TINYPY_INCREF(identifier);
        return identifier;
    }
    buffer = (char *)tinypy_internal_compiler_arena_allocate(arena, class_size + name_size + 1U);
    if (buffer == NULL) {
        return NULL;
    }
    buffer[0] = '_';
    (void)memcpy(buffer + 1U, class_name, class_size);
    (void)memcpy(buffer + 1U + class_size, name, name_size);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(identifier);
    tinypy_value_t *result = tinypy_string_from_bytes(vm, buffer, class_size + name_size + 1U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_tuple_new(tinypy_value_t *owner, tinypy_compiler_size_t size) {
    size_t allocation_size;
    size_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    if (size == 0) {
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    allocation_size = offsetof(tinypy_tuple_object_t, items) + (size_t)size * sizeof(tinypy_value_t *);
    tinypy_tuple_object_t *tuple = (tinypy_tuple_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_TUPLE, allocation_size);
    tuple->base.size = size;
    tinypy_value_t *none = tinypy_none_get(vm);
    for (index = 0U; index < (size_t)size; ++index) {
        tuple->items[index] = none;
        TINYPY_INCREF(none);
    }
    TINYPY_DECREF(none);
    return &tuple->base.base;
}
//////////////////////////////////////////////////////////////////////////
void __tinypy_frontend_tuple_set(tinypy_value_t *tuple_value, tinypy_compiler_size_t index, tinypy_value_t *value) {
    tinypy_tuple_object_t *tuple = TINYPY_TUPLE_OBJECT(tuple_value);
    TINYPY_DECREF(tuple->items[index]);
    tuple->items[index] = value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_dict_keys(tinypy_value_t *dict) {
    tinypy_compiler_size_t position = 0;
    tinypy_value_t *key;
    tinypy_value_t *value;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
    while (__tinypy_frontend_dict_next(dict, &position, &key, &value) != 0) {
        tinypy_list_append(list, key);
    }
    return list;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_frontend_string_compare(tinypy_value_t *left, tinypy_value_t *right) {
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;
    size_t common;
    int32_t result;

    left_bytes = (const uint8_t *)tinypy_string_view(left, &left_size);
    right_bytes = (const uint8_t *)tinypy_string_view(right, &right_size);
    common = left_size < right_size ? left_size : right_size;
    result = memcmp(left_bytes, right_bytes, common);
    if (result != 0) {
        return result;
    }
    return left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_list_sort(tinypy_value_t *list) {
    tinypy_list_object_t *list_object = TINYPY_LIST_OBJECT(list);
    size_t size = TINYPY_SIZED_SIZE(list);
    size_t index;

    for (index = 1U; index < size; ++index) {
        tinypy_value_t *item = list_object->items[index];
        size_t position = index;

        while (position != 0U && __tinypy_frontend_string_compare(list_object->items[position - 1U], item) > 0) {
            list_object->items[position] = list_object->items[position - 1U];
            position -= 1U;
        }
        list_object->items[position] = item;
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_list_as_tuple(tinypy_value_t *list) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(list)->items, TINYPY_SIZED_SIZE(list));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_sequence_list(tinypy_value_t *sequence) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_value_t *return_value_1 = tinypy_list_from_items(vm, (tinypy_value_t *const *)TINYPY_TUPLE_OBJECT(sequence)->items, TINYPY_SIZED_SIZE(sequence));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_frontend_string_allocate(tinypy_vm_t *vm, size_t size) {
    size_t allocation_size;

    if (size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    allocation_size = offsetof(tinypy_string_object_t, bytes) + size + 1U;
    tinypy_string_object_t *string = (tinypy_string_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_STRING, allocation_size);
    string->base.size = size;
    (void)memset(string->bytes, 0, size + 1U);
    return &string->base.base;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_frontend_string_resize(tinypy_value_t **string, tinypy_compiler_size_t size) {
    size_t old_size;
    const void *old_bytes;
    size_t copy_size;

    tinypy_value_t *old_value = *string;
    old_bytes = tinypy_string_view(old_value, &old_size);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(old_value);
    tinypy_value_t *new_value = __tinypy_frontend_string_allocate(vm, (size_t)size);
    copy_size = old_size < (size_t)size ? old_size : (size_t)size;
    if (copy_size != 0U) {
        (void)memcpy(TINYPY_STRING_OBJECT(new_value)->bytes, old_bytes, copy_size);
    }
    TINYPY_DECREF(old_value);
    *string = new_value;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_string_uninitialized(tinypy_value_t *owner, size_t size) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    tinypy_value_t *return_value_1 = __tinypy_frontend_string_allocate(vm, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_frontend_pointer_handle(tinypy_value_t *owner, const void *pointer) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, &pointer, sizeof(pointer));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void *__tinypy_frontend_pointer_from_handle(tinypy_value_t *handle) {
    size_t size;
    const void *bytes = tinypy_string_view(handle, &size);
    void *pointer;

    (void)memcpy(&pointer, bytes, sizeof(pointer));
    return pointer;
}
//////////////////////////////////////////////////////////////////////////
tinypy_compiler_size_t __tinypy_frontend_object_size(tinypy_value_t *value) {
    tinypy_value_type_e type = tinypy_typeof(value);

    if (type == TINYPY_VALUE_STRING || type == TINYPY_VALUE_UNICODE || type == TINYPY_VALUE_TUPLE || type == TINYPY_VALUE_LIST) {
        tinypy_compiler_size_t return_value_1 = (tinypy_compiler_size_t)TINYPY_SIZED_SIZE(value);
        return return_value_1;
    }
    if (type == TINYPY_VALUE_DICT) {
        tinypy_compiler_size_t return_value_2 = (tinypy_compiler_size_t)TINYPY_DICT_OBJECT(value)->used;
        return return_value_2;
    }
    if (type == TINYPY_VALUE_SET || type == TINYPY_VALUE_FROZENSET) {
        tinypy_compiler_size_t return_value_3 = (tinypy_compiler_size_t)tinypy_set_size(value);
        return return_value_3;
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
void __tinypy_frontend_clear_raised(tinypy_value_t *owner) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
size_t __tinypy_frontend_constant_size(tinypy_value_t *value) {
    tinypy_value_type_e type = tinypy_typeof(value);

    if (type == TINYPY_VALUE_STRING) {
        size_t size;

        (void)tinypy_string_view(value, &size);
        return size;
    }
    if (type == TINYPY_VALUE_UNICODE) {
        size_t size;
        size_t code_points;

        (void)tinypy_unicode_utf8_view(value, &size, &code_points);
        return size;
    }
    if (type == TINYPY_VALUE_LONG) {
        size_t return_value_1 = TINYPY_LONG_DIGIT_COUNT(value) * sizeof(uint16_t);
        return return_value_1;
    }
    if (type == TINYPY_VALUE_TUPLE) {
        size_t total = 0U;
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(value);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(value);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            size_t item_size = __tinypy_frontend_constant_size(item);

            total += item_size;
        }
        return total;
    }
    return sizeof(value);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_frontend_constant_key_with_extra(tinypy_value_t *object, tinypy_value_t *extra) {

    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    tinypy_value_type_e typeof = tinypy_typeof(object);
    tinypy_value_t *tag = tinypy_integer_from_i64(vm, (int64_t)typeof);
    tinypy_value_t *key = __tinypy_frontend_tuple_new(object, extra != NULL ? 3 : 2);
    TINYPY_COMPILER_TUPLE_SET_ITEM(key, 0, tag);
    TINYPY_INCREF(object);
    TINYPY_COMPILER_TUPLE_SET_ITEM(key, 1, object);
    if (extra != NULL) {
        TINYPY_COMPILER_TUPLE_SET_ITEM(key, 2, extra);
    }
    return key;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *__tinypy_bytecode_constant_key(tinypy_value_t *object) {
    tinypy_value_type_e type;

    type = tinypy_typeof(object);
    if (type == TINYPY_VALUE_FLOAT) {
        double value = tinypy_float_as_double(object);
        tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
        tinypy_value_t *bits = tinypy_string_from_bytes(vm, &value, sizeof(value));

        tinypy_value_t *return_value_1 = __tinypy_frontend_constant_key_with_extra(object, bits);
        return return_value_1;
    }
    if (type == TINYPY_VALUE_COMPLEX) {
        double values[2];
        tinypy_value_t *bits;

        tinypy_complex_as_doubles(object, &values[0], &values[1]);
        tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
        bits = tinypy_string_from_bytes(vm, values, sizeof(values));
        tinypy_value_t *return_value_2 = __tinypy_frontend_constant_key_with_extra(object, bits);
        return return_value_2;
    }
    if (type == TINYPY_VALUE_TUPLE) {
        size_t size = TINYPY_TUPLE_SIZE(object);
        tinypy_value_t *nested = __tinypy_frontend_tuple_new(object, (tinypy_compiler_size_t)size);
        size_t index;

        for (index = 0U; index < size; ++index) {
            TINYPY_COMPILER_TUPLE_SET_ITEM(nested, (tinypy_compiler_size_t)index, __tinypy_bytecode_constant_key(TINYPY_TUPLE_GET(object, index)));
        }
        tinypy_value_t *return_value_3 = __tinypy_frontend_constant_key_with_extra(object, nested);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = __tinypy_frontend_constant_key_with_extra(object, NULL);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
tinypy_code_object_t *__tinypy_frontend_code_new(int32_t arg_count, int32_t local_count, int32_t stack_size, int32_t flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int32_t first_line_number, tinypy_value_t *lnotab) {
    tinypy_code_object_t *return_value_1 = (tinypy_code_object_t *)tinypy_code_new(arg_count, local_count, stack_size, flags, bytecode, consts, names, varnames, freevars, cellvars, filename, name, first_line_number, lnotab);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_ast_sequence_t *tinypy_internal_compiler_ast_sequence_new(int32_t size, tinypy_compile_ctx_t *arena) {
    size_t allocation_size;

    allocation_size = offsetof(tinypy_ast_sequence_t, elements) + (size_t)size * sizeof(void *);
    if (allocation_size == 0U) {
        allocation_size = 1U;
    }
    tinypy_ast_sequence_t *sequence = (tinypy_ast_sequence_t *)tinypy_internal_compiler_arena_allocate(arena, allocation_size);
    if (sequence == NULL) {
        return NULL;
    }
    sequence->size = size;
    return sequence;
}
//////////////////////////////////////////////////////////////////////////
tinypy_ast_integer_sequence_t *tinypy_internal_compiler_ast_integer_sequence_new(int32_t size, tinypy_compile_ctx_t *arena) {
    size_t allocation_size;

    allocation_size = offsetof(tinypy_ast_integer_sequence_t, elements) + (size_t)size * sizeof(int32_t);
    if (allocation_size == 0U) {
        allocation_size = 1U;
    }
    tinypy_ast_integer_sequence_t *sequence = (tinypy_ast_integer_sequence_t *)tinypy_internal_compiler_arena_allocate(arena, allocation_size);
    if (sequence == NULL) {
        return NULL;
    }
    sequence->size = size;
    return sequence;
}
