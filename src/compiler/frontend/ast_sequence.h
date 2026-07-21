#ifndef TINYPY_COMPILER_AST_SEQUENCE_H
#define TINYPY_COMPILER_AST_SEQUENCE_H

typedef tinypy_value_t *tinypy_ast_identifier_t;
typedef tinypy_value_t *tinypy_ast_string_t;
typedef tinypy_value_t *tinypy_ast_literal_t;

typedef enum tinypy_compiler_boolean_e {
    TINYPY_COMPILER_FALSE = 0,
    TINYPY_COMPILER_TRUE = 1
} tinypy_compiler_boolean_e;

typedef struct tinypy_ast_sequence_t {
    int size;
    void *elements[1];
} tinypy_ast_sequence_t;

typedef struct tinypy_ast_integer_sequence_t {
    int size;
    int elements[1];
} tinypy_ast_integer_sequence_t;

tinypy_ast_sequence_t *tinypy_internal_compiler_ast_sequence_new(int size, tinypy_compile_ctx_t *arena);
tinypy_ast_integer_sequence_t *tinypy_internal_compiler_ast_integer_sequence_new(int size, tinypy_compile_ctx_t *arena);
#define TINYPY_AST_SEQUENCE_NEW(size, arena) tinypy_internal_compiler_ast_sequence_new((size), (arena))
#define TINYPY_AST_INTEGER_SEQUENCE_NEW(size, arena) tinypy_internal_compiler_ast_integer_sequence_new((size), (arena))

#define TINYPY_AST_SEQUENCE_GET(sequence, index) (sequence)->elements[(index)]
#define TINYPY_AST_SEQUENCE_LENGTH(sequence) ((sequence) == NULL ? 0 : (sequence)->size)
#ifdef TINYPY_COMPILER_DEBUG
#define TINYPY_AST_SEQUENCE_SET(sequence, index, value)                      \
    {                                                                        \
        int __tinypy_ast_index = (index);                                    \
        assert((sequence) != NULL && __tinypy_ast_index < (sequence)->size); \
        (sequence)->elements[__tinypy_ast_index] = (value);                  \
    }
#else
#define TINYPY_AST_SEQUENCE_SET(sequence, index, value) (sequence)->elements[(index)] = (value)
#endif

#endif
