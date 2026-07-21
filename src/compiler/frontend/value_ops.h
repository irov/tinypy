#ifndef TINYPY_COMPILER_VALUE_OPS_H
#define TINYPY_COMPILER_VALUE_OPS_H

#include "../internal.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

typedef ptrdiff_t tinypy_compiler_size_t;
typedef struct tinypy_compiler_flags_t {
    int flags;
} tinypy_compiler_flags_t;

typedef struct tinypy_compiler_complex_t {
    double real;
    double imag;
} tinypy_compiler_complex_t;

#define TINYPY_COMPILER_CHARMASK(character) ((unsigned char)(character))
#define TINYPY_COMPILER_ISALPHA(character) ((((unsigned int)(unsigned char)(character) | 0x20U) - (unsigned int)'a') < 26U)
#define TINYPY_COMPILER_ISALNUM(character) (TINYPY_COMPILER_ISALPHA(character) || ((unsigned int)(unsigned char)(character) - (unsigned int)'0') < 10U)
#define TINYPY_TOKENIZER_END_OF_INPUT (-1)
#define TINYPY_COMPILER_FLAG_SOURCE_IS_UTF8 0x0100
#define TINYPY_COMPILER_FLAG_DONT_IMPLY_DEDENT 0x0200
#define TINYPY_COMPILER_EXC_VALUE_ERROR 0
#define TINYPY_COMPILER_EXC_SYNTAX_ERROR 0
#define TINYPY_COMPILER_EXC_SYNTAX_WARNING 0
#define TINYPY_COMPILER_EXC_SYSTEM_ERROR 0
#define TINYPY_COMPILER_EXC_UNICODE_ERROR 0
#define TINYPY_COMPILER_EXC_OVERFLOW_ERROR 0
#define TINYPY_COMPILER_EXC_RUNTIME_ERROR 0
#define TINYPY_COMPILER_EXC_KEY_ERROR 0
#define TINYPY_COMPILER_EXC_DEPRECATION_WARNING 0
#define TINYPY_COMPILER_ERR_SET_STRING(exception, message) ((void)(exception), (void)(message))
#define TINYPY_COMPILER_ERR_FORMAT(...) ((void)0)
#define TINYPY_COMPILER_ERR_BAD_INTERNAL_CALL() ((void)0)
#define TINYPY_COMPILER_ERR_OCCURRED() (1)
#define TINYPY_COMPILER_ERR_CLEAR() ((void)0)
#define TINYPY_COMPILER_ERR_NO_MEMORY() ((void)0)
#define TINYPY_COMPILER_ERR_WARN_EXPLICIT(exception, message, filename, line, module, registry) ((void)(exception), (void)(message), (void)(filename), (void)(line), (void)(module), (void)(registry), 0)
#define TINYPY_COMPILER_ARENA_MALLOC(arena, size) tinypy_internal_compiler_arena_allocate((arena), (size))
#define TINYPY_COMPILER_ARENA_ADD_VALUE(arena, value) tinypy_internal_compiler_arena_add_value((arena), (value))
#define TINYPY_COMPILER_INCREF(value) tinypy_retain((tinypy_value_t *)(value))
#define TINYPY_COMPILER_XINCREF(value)                \
    do { \
        if ((value) != NULL)                          \
            tinypy_retain((tinypy_value_t *)(value)); \
    } while (0)
#define TINYPY_COMPILER_DECREF(value) tinypy_release((tinypy_value_t *)(value))
#define TINYPY_COMPILER_XDECREF(value)                 \
    do { \
        if ((value) != NULL)                           \
            tinypy_release((tinypy_value_t *)(value)); \
    } while (0)
#define TINYPY_COMPILER_CLEAR(value)                                          \
    do { \
        if ((value) != NULL) { \
            tinypy_value_t *__tinypy_clear_value = (tinypy_value_t *)(value); \
            (value) = NULL;                                                   \
            tinypy_release(__tinypy_clear_value);                             \
        }                                                                     \
    } while (0)
#define TINYPY_COMPILER_XSETREF(destination, value)         \
    do { \
        tinypy_value_t *__tinypy_old_value = (destination); \
        (destination) = (value);                            \
        TINYPY_COMPILER_XDECREF(__tinypy_old_value);        \
    } while (0)
#define TINYPY_COMPILER_OBJECT_IS_TRUE(value) ((int)tinypy_truth((value), NULL))
#define TINYPY_COMPILER_STRING_CHECK(value) (tinypy_typeof((value)) == TINYPY_VALUE_STRING)
#define TINYPY_COMPILER_UNICODE_CHECK(value) (tinypy_typeof((value)) == TINYPY_VALUE_UNICODE)
#define TINYPY_COMPILER_STRING_AS_STRING(value) ((char *)__tinypy_frontend_string_data((value)))
#define TINYPY_COMPILER_BYTES_AS_STRING(value) TINYPY_COMPILER_STRING_AS_STRING(value)
#define TINYPY_COMPILER_STRING_GET_SIZE(value) ((tinypy_compiler_size_t)__tinypy_frontend_string_size((value)))
#define TINYPY_COMPILER_STRING_SIZE(value) ((tinypy_compiler_size_t)__tinypy_frontend_string_size((value)))
#define TINYPY_COMPILER_INT_CHECK(value) (tinypy_typeof((value)) == TINYPY_VALUE_INTEGER || tinypy_typeof((value)) == TINYPY_VALUE_BOOL)
#define TINYPY_COMPILER_INT_AS_LONG(value) ((long)tinypy_integer_as_i64((value)))
#define TINYPY_COMPILER_DICT_GET_ITEM(dict, key) __tinypy_frontend_dict_get((dict), (key))
#define TINYPY_COMPILER_DICT_SET_ITEM(dict, key, value) __tinypy_frontend_dict_set((dict), (key), (value))
#define TINYPY_COMPILER_DICT_DEL_ITEM(dict, key) __tinypy_frontend_dict_delete((dict), (key))
#define TINYPY_COMPILER_DICT_UPDATE(dict, source) __tinypy_frontend_dict_update((dict), (source))
#define TINYPY_COMPILER_DICT_NEXT(dict, position, key, value) __tinypy_frontend_dict_next((dict), (position), (key), (value))
#define TINYPY_COMPILER_DICT_SIZE(dict) ((tinypy_compiler_size_t)tinypy_dict_size((dict)))
#define TINYPY_COMPILER_LIST_APPEND(list, value) __tinypy_frontend_list_append((list), (value))
#define TINYPY_COMPILER_LIST_GET_SIZE(list) ((tinypy_compiler_size_t)tinypy_list_size((list)))
#define TINYPY_COMPILER_LIST_GET_ITEM(list, index) tinypy_list_get((list), (size_t)(index))
#define TINYPY_COMPILER_LIST_SIZE(list) ((tinypy_compiler_size_t)tinypy_list_size((list)))
#define TINYPY_COMPILER_LIST_CHECK(list) (tinypy_typeof((list)) == TINYPY_VALUE_LIST)
#define TINYPY_COMPILER_LIST_CHECK_EXACT(list) TINYPY_COMPILER_LIST_CHECK(list)
#define TINYPY_COMPILER_SEQUENCE_DEL_ITEM(list, index) __tinypy_frontend_list_delete((list), (index))
#define TINYPY_COMPILER_TUPLE_GET_SIZE(tuple) ((tinypy_compiler_size_t)tinypy_tuple_size((tuple)))
#define TINYPY_COMPILER_TUPLE_SIZE(tuple) ((tinypy_compiler_size_t)tinypy_tuple_size((tuple)))
#define TINYPY_COMPILER_TUPLE_GET_ITEM(tuple, index) tinypy_tuple_get((tuple), (size_t)(index))
#define TINYPY_COMPILER_TUPLE_SET_ITEM(tuple, index, value) __tinypy_frontend_tuple_set((tuple), (index), (value))

size_t __tinypy_frontend_string_size(const tinypy_value_t *value);
const void *__tinypy_frontend_string_data(const tinypy_value_t *value);
int __tinypy_frontend_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value);
tinypy_value_t *__tinypy_frontend_dict_get(tinypy_value_t *dict, tinypy_value_t *key);
int __tinypy_frontend_dict_delete(tinypy_value_t *dict, tinypy_value_t *key);
int __tinypy_frontend_dict_update(tinypy_value_t *dict, tinypy_value_t *source);
int __tinypy_frontend_dict_next(tinypy_value_t *dict, tinypy_compiler_size_t *position, tinypy_value_t **key, tinypy_value_t **value);
int __tinypy_frontend_list_append(tinypy_value_t *list, tinypy_value_t *value);
int __tinypy_frontend_list_delete(tinypy_value_t *list, tinypy_compiler_size_t index);
tinypy_value_t *__tinypy_frontend_dict_new_from_owner(tinypy_value_t *owner);
tinypy_value_t *__tinypy_frontend_integer_from_owner(tinypy_value_t *owner, int64_t value);
tinypy_value_t *__tinypy_frontend_string_from_owner(tinypy_value_t *owner, const char *bytes, size_t size);
int __tinypy_frontend_dict_set_none(tinypy_value_t *dict, tinypy_value_t *key);
tinypy_value_t *__tinypy_frontend_format_identifier(tinypy_value_t *owner, const char *prefix, int value, const char *suffix);
tinypy_value_t *__tinypy_frontend_mangle(tinypy_compile_ctx_t *arena, tinypy_value_t *private_name, tinypy_value_t *identifier);
tinypy_value_t *__tinypy_frontend_tuple_new(tinypy_value_t *owner, tinypy_compiler_size_t size);
void __tinypy_frontend_tuple_set(tinypy_value_t *tuple, tinypy_compiler_size_t index, tinypy_value_t *value);
tinypy_value_t *__tinypy_frontend_dict_keys(tinypy_value_t *dict);
int __tinypy_frontend_list_sort(tinypy_value_t *list);
tinypy_value_t *__tinypy_frontend_list_as_tuple(tinypy_value_t *list);
tinypy_value_t *__tinypy_frontend_sequence_list(tinypy_value_t *sequence);
int __tinypy_frontend_string_resize(tinypy_value_t **string, tinypy_compiler_size_t size);
tinypy_value_t *__tinypy_frontend_string_uninitialized(tinypy_value_t *owner, size_t size);
tinypy_value_t *__tinypy_frontend_pointer_handle(tinypy_value_t *owner, const void *pointer);
void *__tinypy_frontend_pointer_from_handle(tinypy_value_t *handle);

#define TINYPY_COMPILER_DICT_KEYS(dict) __tinypy_frontend_dict_keys((dict))
#define TINYPY_COMPILER_LIST_SORT(list) __tinypy_frontend_list_sort((list))
#define TINYPY_COMPILER_LIST_AS_TUPLE(list) __tinypy_frontend_list_as_tuple((list))
#define TINYPY_COMPILER_SEQUENCE_LIST(sequence) __tinypy_frontend_sequence_list((sequence))
#define TINYPY_COMPILER_STRING_RESIZE(string, size) __tinypy_frontend_string_resize((string), (size))

static inline int __tinypy_frontend_is_digit(int character) {
    return (unsigned int)(unsigned char)character - (unsigned int)'0' < 10U;
}

static inline int __tinypy_frontend_is_hex_digit(int character) {
    unsigned int byte = (unsigned int)(unsigned char)character;

    return byte - (unsigned int)'0' < 10U || (byte | 0x20U) - (unsigned int)'a' < 6U;
}

static inline int __tinypy_frontend_ascii_to_integer(const char *text) {
    int value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text += 1;
    }
    return value;
}

#define isdigit(character) __tinypy_frontend_is_digit(character)
#define isxdigit(character) __tinypy_frontend_is_hex_digit(character)
#define atoi(text) __tinypy_frontend_ascii_to_integer(text)
#define TINYPY_COMPILER_NUMBER_MULTIPLY(left, right) tinypy_multiply((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_TRUE_DIVIDE(left, right) tinypy_true_divide((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_FLOOR_DIVIDE(left, right) tinypy_floor_divide((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_REMAINDER(left, right) tinypy_remainder((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_ADD(left, right) tinypy_add((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_SUBTRACT(left, right) tinypy_subtract((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_LSHIFT(left, right) tinypy_left_shift((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_RSHIFT(left, right) tinypy_right_shift((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_AND(left, right) tinypy_bit_and((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_XOR(left, right) tinypy_bit_xor((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_OR(left, right) tinypy_bit_or((left), (right), NULL)
#define TINYPY_COMPILER_NUMBER_NEGATIVE(value) tinypy_negative((value), NULL)
#define TINYPY_COMPILER_NUMBER_INVERT(value) tinypy_invert((value), NULL)
#define TINYPY_COMPILER_OBJECT_GET_ITEM(container, key) tinypy_get_item((container), (key), NULL)
#define TINYPY_COMPILER_OBJECT_REPR(value) tinypy_object_repr((value), NULL)
#define TINYPY_COMPILER_OBJECT_SIZE(value) __tinypy_frontend_object_size((value))

tinypy_compiler_size_t __tinypy_frontend_object_size(tinypy_value_t *value);
void __tinypy_frontend_clear_raised(tinypy_value_t *owner);
size_t __tinypy_frontend_constant_size(tinypy_value_t *value);

#endif
