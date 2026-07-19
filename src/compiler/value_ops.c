#include "internal.h"

#include "value_ops.h"
#include "ast_sequence.h"
#include "bytecode_builder.h"

size_t __tinypy_frontend_string_size(const tinypy_value_t *value)
{
    size_t size;

    assert(value != NULL);
    (void)tinypy_string_view(value, &size);
    return size;
}

const void *__tinypy_frontend_string_data(const tinypy_value_t *value)
{
    size_t size;

    assert(value != NULL);
    return tinypy_string_view(value, &size);
}

int __tinypy_frontend_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value)
{
    tinypy_dict_set(dict, key, value);
    return 0;
}

tinypy_value_t *__tinypy_frontend_dict_get(tinypy_value_t *dict, tinypy_value_t *key)
{
    if (tinypy_dict_contains(dict, key) == 0) return NULL;
    return tinypy_dict_get(dict, key);
}

int __tinypy_frontend_dict_delete(tinypy_value_t *dict, tinypy_value_t *key)
{
    if (tinypy_dict_contains(dict, key) == 0) return -1;
    tinypy_dict_delete(dict, key);
    return 0;
}

int __tinypy_frontend_dict_update(tinypy_value_t *dict, tinypy_value_t *source)
{
    tinypy_compiler_size_t position = 0;
    tinypy_value_t *key;
    tinypy_value_t *value;

    while (__tinypy_frontend_dict_next(source, &position, &key, &value) != 0) tinypy_dict_set(dict, key, value);
    return 0;
}

int __tinypy_frontend_dict_next(tinypy_value_t *dict, tinypy_compiler_size_t *position, tinypy_value_t **key, tinypy_value_t **value)
{
    size_t capacity;
    size_t index;

    assert(dict != NULL);
    assert(position != NULL);
    assert(key != NULL);
    assert(value != NULL);
    capacity = TINYPY_DICT_OBJECT(dict)->mask + 1U;
    index = (size_t)*position;
    while (index < capacity) {
        tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[index];

        index += 1U;
        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) continue;
        *position = (tinypy_compiler_size_t)index;
        *key = entry->key;
        *value = entry->value;
        return 1;
    }
    *position = (tinypy_compiler_size_t)capacity;
    return 0;
}

int __tinypy_frontend_list_append(tinypy_value_t *list, tinypy_value_t *value)
{
    tinypy_list_append(list, value);
    return 0;
}

int __tinypy_frontend_list_delete(tinypy_value_t *list, tinypy_compiler_size_t index)
{
    assert(index >= 0);
    tinypy_list_delete(list, (size_t)index);
    return 0;
}

tinypy_value_t *__tinypy_frontend_dict_new_from_owner(tinypy_value_t *owner)
{
    assert(owner != NULL);
    return tinypy_dict_new(tinypy_internal_value_vm(owner));
}

tinypy_value_t *__tinypy_frontend_integer_from_owner(tinypy_value_t *owner, int64_t value)
{
    assert(owner != NULL);
    return tinypy_integer_from_i64(tinypy_internal_value_vm(owner), value);
}

tinypy_value_t *__tinypy_frontend_string_from_owner(tinypy_value_t *owner, const char *bytes, size_t size)
{
    assert(owner != NULL);
    return tinypy_string_from_bytes(tinypy_internal_value_vm(owner), bytes, size);
}

int __tinypy_frontend_dict_set_none(tinypy_value_t *dict, tinypy_value_t *key)
{
    tinypy_value_t *none;

    assert(dict != NULL);
    none = tinypy_none_get(tinypy_internal_value_vm(dict));
    tinypy_dict_set(dict, key, none);
    tinypy_release(none);
    return 0;
}

tinypy_value_t *__tinypy_frontend_format_identifier(tinypy_value_t *owner, const char *prefix, int value, const char *suffix)
{
    char reversed[32];
    char buffer[96];
    size_t prefix_size;
    size_t suffix_size;
    size_t digit_count = 0U;
    size_t index;
    unsigned int magnitude;
    size_t output_size;

    assert(owner != NULL);
    assert(prefix != NULL);
    assert(suffix != NULL);
    prefix_size = strlen(prefix);
    suffix_size = strlen(suffix);
    magnitude = value < 0 ? (unsigned int)(-(value + 1)) + 1U : (unsigned int)value;
    do {
        reversed[digit_count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    output_size = prefix_size + digit_count + suffix_size + (value < 0 ? 1U : 0U);
    assert(output_size < sizeof(buffer));
    (void)memcpy(buffer, prefix, prefix_size);
    index = prefix_size;
    if (value < 0) buffer[index++] = '-';
    while (digit_count != 0U) buffer[index++] = reversed[--digit_count];
    (void)memcpy(buffer + index, suffix, suffix_size);
    return __tinypy_frontend_string_from_owner(owner, buffer, output_size);
}

tinypy_value_t *__tinypy_frontend_mangle(tinypy_compile_ctx_t *arena, tinypy_value_t *private_name, tinypy_value_t *identifier)
{
    const char *name;
    const char *class_name;
    size_t name_size;
    size_t class_size;
    char *buffer;
    tinypy_value_t *result;

    assert(arena != NULL);
    assert(identifier != NULL);
    name = (const char *)__tinypy_frontend_string_data(identifier);
    name_size = __tinypy_frontend_string_size(identifier);
    if (private_name == NULL || name_size < 2U || name[0] != '_' || name[1] != '_' || (name[name_size - 1U] == '_' && name[name_size - 2U] == '_') || memchr(name, '.', name_size) != NULL) {
        tinypy_retain(identifier);
        return identifier;
    }
    class_name = (const char *)__tinypy_frontend_string_data(private_name);
    class_size = __tinypy_frontend_string_size(private_name);
    while (class_size != 0U && *class_name == '_') {
        class_name += 1;
        class_size -= 1U;
    }
    if (class_size == 0U) {
        tinypy_retain(identifier);
        return identifier;
    }
    assert(class_size <= SIZE_MAX - name_size - 1U);
    buffer = (char *)tinypy_internal_compiler_arena_allocate(arena, class_size + name_size + 1U);
    if (buffer == NULL) return NULL;
    buffer[0] = '_';
    (void)memcpy(buffer + 1U, class_name, class_size);
    (void)memcpy(buffer + 1U + class_size, name, name_size);
    result = tinypy_string_from_bytes(tinypy_internal_value_vm(identifier), buffer, class_size + name_size + 1U);
    return result;
}

tinypy_value_t *__tinypy_frontend_tuple_new(tinypy_value_t *owner, tinypy_compiler_size_t size)
{
    tinypy_vm_t *vm;
    tinypy_tuple_object_t *tuple;
    tinypy_value_t *none;
    size_t allocation_size;
    size_t index;

    assert(owner != NULL);
    assert(size >= 0);
    vm = tinypy_internal_value_vm(owner);
    if (size == 0) return tinypy_tuple_from_items(vm, NULL, 0U);
    assert((size_t)size <= (SIZE_MAX - offsetof(tinypy_tuple_object_t, items)) / sizeof(tinypy_value_t *));
    allocation_size = offsetof(tinypy_tuple_object_t, items) + (size_t)size * sizeof(tinypy_value_t *);
    tuple = (tinypy_tuple_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_TUPLE, allocation_size);
    tuple->base.size = size;
    none = tinypy_none_get(vm);
    for (index = 0U; index < (size_t)size; index += 1U) {
        tuple->items[index] = none;
        tinypy_retain(none);
    }
    tinypy_release(none);
    return &tuple->base.base;
}

void __tinypy_frontend_tuple_set(tinypy_value_t *tuple_value, tinypy_compiler_size_t index, tinypy_value_t *value)
{
    tinypy_tuple_object_t *tuple;

    assert(tuple_value != NULL);
    assert(value != NULL);
    assert(index >= 0 && index < TINYPY_COMPILER_TUPLE_GET_SIZE(tuple_value));
    tuple = TINYPY_TUPLE_OBJECT(tuple_value);
    tinypy_release(tuple->items[index]);
    tuple->items[index] = value;
}

tinypy_value_t *__tinypy_frontend_dict_keys(tinypy_value_t *dict)
{
    tinypy_value_t *list;
    tinypy_compiler_size_t position = 0;
    tinypy_value_t *key;
    tinypy_value_t *value;

    list = tinypy_list_from_items(tinypy_internal_value_vm(dict), NULL, 0U);
    while (__tinypy_frontend_dict_next(dict, &position, &key, &value) != 0) tinypy_list_append(list, key);
    return list;
}

static int __tinypy_frontend_string_compare(tinypy_value_t *left, tinypy_value_t *right)
{
    const unsigned char *left_bytes;
    const unsigned char *right_bytes;
    size_t left_size;
    size_t right_size;
    size_t common;
    int result;

    left_bytes = (const unsigned char *)tinypy_string_view(left, &left_size);
    right_bytes = (const unsigned char *)tinypy_string_view(right, &right_size);
    common = left_size < right_size ? left_size : right_size;
    result = memcmp(left_bytes, right_bytes, common);
    if (result != 0) return result;
    return left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
}

int __tinypy_frontend_list_sort(tinypy_value_t *list)
{
    tinypy_list_object_t *list_object = TINYPY_LIST_OBJECT(list);
    size_t size = (size_t)TINYPY_SIZE(list);
    size_t index;

    for (index = 1U; index < size; index += 1U) {
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

tinypy_value_t *__tinypy_frontend_list_as_tuple(tinypy_value_t *list)
{
    return tinypy_tuple_from_items(tinypy_internal_value_vm(list), TINYPY_LIST_OBJECT(list)->items, (size_t)TINYPY_SIZE(list));
}

tinypy_value_t *__tinypy_frontend_sequence_list(tinypy_value_t *sequence)
{
    assert(tinypy_typeof(sequence) == TINYPY_VALUE_TUPLE);
    return tinypy_list_from_items(tinypy_internal_value_vm(sequence), (tinypy_value_t *const *)TINYPY_TUPLE_OBJECT(sequence)->items, (size_t)TINYPY_SIZE(sequence));
}

static tinypy_value_t *__tinypy_frontend_string_allocate(tinypy_vm_t *vm, size_t size)
{
    tinypy_string_object_t *string;
    size_t allocation_size;

    if (size == 0U) return tinypy_string_from_bytes(vm, NULL, 0U);
    assert(size <= SIZE_MAX - offsetof(tinypy_string_object_t, bytes) - 1U);
    allocation_size = offsetof(tinypy_string_object_t, bytes) + size + 1U;
    string = (tinypy_string_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_STRING, allocation_size);
    string->base.size = (ptrdiff_t)size;
    (void)memset(string->bytes, 0, size + 1U);
    return &string->base.base;
}

int __tinypy_frontend_string_resize(tinypy_value_t **string, tinypy_compiler_size_t size)
{
    tinypy_value_t *old_value;
    tinypy_value_t *new_value;
    size_t old_size;
    const void *old_bytes;
    size_t copy_size;

    assert(string != NULL);
    assert(*string != NULL);
    assert(size >= 0);
    old_value = *string;
    old_bytes = tinypy_string_view(old_value, &old_size);
    new_value = __tinypy_frontend_string_allocate(tinypy_internal_value_vm(old_value), (size_t)size);
    copy_size = old_size < (size_t)size ? old_size : (size_t)size;
    if (copy_size != 0U) (void)memcpy(TINYPY_STRING_OBJECT(new_value)->bytes, old_bytes, copy_size);
    tinypy_release(old_value);
    *string = new_value;
    return 0;
}

tinypy_value_t *__tinypy_frontend_string_uninitialized(tinypy_value_t *owner, size_t size)
{
    assert(owner != NULL);
    return __tinypy_frontend_string_allocate(tinypy_internal_value_vm(owner), size);
}

tinypy_value_t *__tinypy_frontend_pointer_handle(tinypy_value_t *owner, const void *pointer)
{
    return tinypy_string_from_bytes(tinypy_internal_value_vm(owner), &pointer, sizeof(pointer));
}

void *__tinypy_frontend_pointer_from_handle(tinypy_value_t *handle)
{
    size_t size;
    const void *bytes = tinypy_string_view(handle, &size);
    void *pointer;

    assert(size == sizeof(pointer));
    (void)memcpy(&pointer, bytes, sizeof(pointer));
    return pointer;
}

tinypy_compiler_size_t __tinypy_frontend_object_size(tinypy_value_t *value)
{
    tinypy_value_type_e type = tinypy_typeof(value);

    if (type == TINYPY_VALUE_STRING || type == TINYPY_VALUE_UNICODE || type == TINYPY_VALUE_TUPLE || type == TINYPY_VALUE_LIST) return (tinypy_compiler_size_t)TINYPY_SIZE(value);
    if (type == TINYPY_VALUE_DICT) return (tinypy_compiler_size_t)TINYPY_DICT_OBJECT(value)->used;
    if (type == TINYPY_VALUE_SET || type == TINYPY_VALUE_FROZENSET) return (tinypy_compiler_size_t)tinypy_set_size(value);
    return -1;
}

void __tinypy_frontend_clear_raised(tinypy_value_t *owner)
{
    assert(owner != NULL);
    tinypy_internal_exception_clear_raised(tinypy_internal_value_vm(owner));
}

size_t __tinypy_frontend_constant_size(tinypy_value_t *value)
{
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
    if (type == TINYPY_VALUE_LONG) return TINYPY_LONG_DIGIT_COUNT(value) * sizeof(uint16_t);
    if (type == TINYPY_VALUE_TUPLE) {
        size_t total = 0U;
        size_t index;

        for (index = 0U; index < tinypy_tuple_size(value); index += 1U) {
            size_t item_size = __tinypy_frontend_constant_size(tinypy_tuple_get(value, index));

            assert(item_size <= SIZE_MAX - total);
            total += item_size;
        }
        return total;
    }
    return sizeof(value);
}

static tinypy_value_t *__tinypy_frontend_constant_key_with_extra(tinypy_value_t *object, tinypy_value_t *extra)
{
    tinypy_value_t *tag;
    tinypy_value_t *key;

    tag = tinypy_integer_from_i64(tinypy_internal_value_vm(object), (int64_t)tinypy_typeof(object));
    key = __tinypy_frontend_tuple_new(object, extra != NULL ? 3 : 2);
    TINYPY_COMPILER_TUPLE_SET_ITEM(key, 0, tag);
    tinypy_retain(object);
    TINYPY_COMPILER_TUPLE_SET_ITEM(key, 1, object);
    if (extra != NULL) TINYPY_COMPILER_TUPLE_SET_ITEM(key, 2, extra);
    return key;
}

tinypy_value_t *__tinypy_bytecode_constant_key(tinypy_value_t *object)
{
    tinypy_value_type_e type;

    assert(object != NULL);
    type = tinypy_typeof(object);
    if (type == TINYPY_VALUE_FLOAT) {
        double value = tinypy_float_as_double(object);
        tinypy_value_t *bits = tinypy_string_from_bytes(tinypy_internal_value_vm(object), &value, sizeof(value));

        return __tinypy_frontend_constant_key_with_extra(object, bits);
    }
    if (type == TINYPY_VALUE_COMPLEX) {
        double values[2];
        tinypy_value_t *bits;

        tinypy_complex_as_doubles(object, &values[0], &values[1]);
        bits = tinypy_string_from_bytes(tinypy_internal_value_vm(object), values, sizeof(values));
        return __tinypy_frontend_constant_key_with_extra(object, bits);
    }
    if (type == TINYPY_VALUE_TUPLE) {
        size_t size = tinypy_tuple_size(object);
        tinypy_value_t *nested = __tinypy_frontend_tuple_new(object, (tinypy_compiler_size_t)size);
        size_t index;

        for (index = 0U; index < size; index += 1U) TINYPY_COMPILER_TUPLE_SET_ITEM(nested, (tinypy_compiler_size_t)index, __tinypy_bytecode_constant_key(tinypy_tuple_get(object, index)));
        return __tinypy_frontend_constant_key_with_extra(object, nested);
    }
    return __tinypy_frontend_constant_key_with_extra(object, NULL);
}

tinypy_code_object_t *__tinypy_frontend_code_new(int arg_count, int local_count, int stack_size, int flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int first_line_number, tinypy_value_t *lnotab)
{
    return (tinypy_code_object_t *)tinypy_code_new(arg_count, local_count, stack_size, flags, bytecode, consts, names, varnames, freevars, cellvars, filename, name, first_line_number, lnotab);
}

tinypy_ast_sequence_t *tinypy_internal_compiler_ast_sequence_new(int size, tinypy_compile_ctx_t *arena)
{
    size_t allocation_size;
    tinypy_ast_sequence_t *sequence;

    assert(size >= 0);
    assert(arena != NULL);
    assert((size_t)size <= (SIZE_MAX - offsetof(tinypy_ast_sequence_t, elements)) / sizeof(void *));
    allocation_size = offsetof(tinypy_ast_sequence_t, elements) + (size_t)size * sizeof(void *);
    if (allocation_size == 0U) allocation_size = 1U;
    sequence = (tinypy_ast_sequence_t *)tinypy_internal_compiler_arena_allocate(arena, allocation_size);
    if (sequence == NULL) return NULL;
    sequence->size = size;
    return sequence;
}

tinypy_ast_integer_sequence_t *tinypy_internal_compiler_ast_integer_sequence_new(int size, tinypy_compile_ctx_t *arena)
{
    size_t allocation_size;
    tinypy_ast_integer_sequence_t *sequence;

    assert(size >= 0);
    assert(arena != NULL);
    assert((size_t)size <= (SIZE_MAX - offsetof(tinypy_ast_integer_sequence_t, elements)) / sizeof(int));
    allocation_size = offsetof(tinypy_ast_integer_sequence_t, elements) + (size_t)size * sizeof(int);
    if (allocation_size == 0U) allocation_size = 1U;
    sequence = (tinypy_ast_integer_sequence_t *)tinypy_internal_compiler_arena_allocate(arena, allocation_size);
    if (sequence == NULL) return NULL;
    sequence->size = size;
    return sequence;
}
