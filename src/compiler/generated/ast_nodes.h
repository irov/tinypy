/* Immutable tinypy AST declarations generated from the Python 2.7 grammar. */

#ifndef TINYPY_COMPILER_GENERATED_AST_NODES_H
#define TINYPY_COMPILER_GENERATED_AST_NODES_H

#include "ast_sequence.h"

typedef struct tinypy_ast_module_s *tinypy_ast_module_t;

typedef struct tinypy_ast_statement_s *tinypy_ast_statement_t;

typedef struct tinypy_ast_expression_s *tinypy_ast_expression_t;

typedef enum tinypy_ast_expression_context_e {
    TINYPY_AST_CONTEXT_LOAD = 1,
    TINYPY_AST_CONTEXT_STORE = 2,
    TINYPY_AST_CONTEXT_DELETE = 3,
    TINYPY_AST_CONTEXT_AUGMENTED_LOAD = 4,
    TINYPY_AST_CONTEXT_AUGMENTED_STORE = 5,
    TINYPY_AST_CONTEXT_PARAMETER = 6
} tinypy_ast_expression_context_e;

typedef struct tinypy_ast_slice_s *tinypy_ast_slice_t;

typedef enum tinypy_ast_boolean_operator_e {
    TINYPY_AST_BOOLEAN_AND = 1,
    TINYPY_AST_BOOLEAN_OR = 2
} tinypy_ast_boolean_operator_e;

typedef enum tinypy_ast_binary_operator_e {
    TINYPY_AST_BINARY_ADD = 1,
    TINYPY_AST_BINARY_SUBTRACT = 2,
    TINYPY_AST_BINARY_MULTIPLY = 3,
    TINYPY_AST_BINARY_DIVIDE = 4,
    TINYPY_AST_BINARY_MODULO = 5,
    TINYPY_AST_BINARY_POWER = 6,
    TINYPY_AST_BINARY_LEFT_SHIFT = 7,
    TINYPY_AST_BINARY_RIGHT_SHIFT = 8,
    TINYPY_AST_BINARY_BIT_OR = 9,
    TINYPY_AST_BINARY_BIT_XOR = 10,
    TINYPY_AST_BINARY_BIT_AND = 11,
    TINYPY_AST_BINARY_FLOOR_DIVIDE = 12
} tinypy_ast_binary_operator_e;

typedef enum tinypy_ast_unary_operator_e {
    TINYPY_AST_UNARY_INVERT = 1,
    TINYPY_AST_UNARY_NOT = 2,
    TINYPY_AST_UNARY_ADD = 3,
    TINYPY_AST_UNARY_SUBTRACT = 4
} tinypy_ast_unary_operator_e;

typedef enum tinypy_ast_compare_operator_e {
    TINYPY_AST_COMPARE_EQUAL = 1,
    TINYPY_AST_COMPARE_NOT_EQUAL = 2,
    TINYPY_AST_COMPARE_LESS = 3,
    TINYPY_AST_COMPARE_LESS_EQUAL = 4,
    TINYPY_AST_COMPARE_GREATER = 5,
    TINYPY_AST_COMPARE_GREATER_EQUAL = 6,
    TINYPY_AST_COMPARE_IS = 7,
    TINYPY_AST_COMPARE_IS_NOT = 8,
    TINYPY_AST_COMPARE_IN = 9,
    TINYPY_AST_COMPARE_NOT_IN = 10
} tinypy_ast_compare_operator_e;

typedef struct tinypy_ast_comprehension_s *tinypy_ast_comprehension_t;

typedef struct tinypy_ast_exception_handler_s *tinypy_ast_exception_handler_t;

typedef struct tinypy_ast_arguments_s *tinypy_ast_arguments_t;

typedef struct tinypy_ast_keyword_s *tinypy_ast_keyword_t;

typedef struct tinypy_ast_alias_s *tinypy_ast_alias_t;

#define TINYPY_AST_SEQUENCE_TYPE_expr tinypy_ast_expression_t
#define TINYPY_AST_SEQUENCE_TYPE_stmt tinypy_ast_statement_t
#define TINYPY_AST_SEQUENCE_TYPE_slice tinypy_ast_slice_t
#define TINYPY_AST_SEQUENCE_TYPE_alias tinypy_ast_alias_t
#define TINYPY_AST_SEQUENCE_TYPE_keyword tinypy_ast_keyword_t
#define TINYPY_AST_SEQUENCE_TYPE_comprehension tinypy_ast_comprehension_t
#define TINYPY_AST_SEQUENCE_TYPE_excepthandler tinypy_ast_exception_handler_t
#define TINYPY_AST_SEQUENCE_TYPE(type) TINYPY_AST_SEQUENCE_TYPE_##type

typedef enum tinypy_ast_module_kind_e {
    TINYPY_AST_KIND_MODULE = 1,
    TINYPY_AST_KIND_INTERACTIVE = 2,
    TINYPY_AST_KIND_EXPRESSION = 3,
    TINYPY_AST_KIND_SUITE = 4
} tinypy_ast_module_kind_e;
struct tinypy_ast_module_s {
    tinypy_ast_module_kind_e kind;
    union {
        struct {
            tinypy_ast_sequence_t *body;
        } Module;

        struct {
            tinypy_ast_sequence_t *body;
        } Interactive;

        struct {
            tinypy_ast_expression_t body;
        } Expression;

        struct {
            tinypy_ast_sequence_t *body;
        } Suite;

    } v;
};

typedef enum tinypy_ast_statement_kind_e {
    TINYPY_AST_KIND_FUNCTION_DEF = 1,
    TINYPY_AST_KIND_CLASS_DEF = 2,
    TINYPY_AST_KIND_RETURN = 3,
    TINYPY_AST_KIND_DELETE = 4,
    TINYPY_AST_KIND_ASSIGN = 5,
    TINYPY_AST_KIND_AUG_ASSIGN = 6,
    TINYPY_AST_KIND_PRINT = 7,
    TINYPY_AST_KIND_FOR = 8,
    TINYPY_AST_KIND_WHILE = 9,
    TINYPY_AST_KIND_IF = 10,
    TINYPY_AST_KIND_WITH = 11,
    TINYPY_AST_KIND_RAISE = 12,
    TINYPY_AST_KIND_TRY_EXCEPT = 13,
    TINYPY_AST_KIND_TRY_FINALLY = 14,
    TINYPY_AST_KIND_ASSERT = 15,
    TINYPY_AST_KIND_IMPORT = 16,
    TINYPY_AST_KIND_IMPORT_FROM = 17,
    TINYPY_AST_KIND_EXEC = 18,
    TINYPY_AST_KIND_GLOBAL = 19,
    TINYPY_AST_KIND_EXPR = 20,
    TINYPY_AST_KIND_PASS = 21,
    TINYPY_AST_KIND_BREAK = 22,
    TINYPY_AST_KIND_CONTINUE = 23
} tinypy_ast_statement_kind_e;
struct tinypy_ast_statement_s {
    tinypy_ast_statement_kind_e kind;
    union {
        struct {
            tinypy_ast_identifier_t name;
            tinypy_ast_arguments_t args;
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *decorator_list;
        } FunctionDef;

        struct {
            tinypy_ast_identifier_t name;
            tinypy_ast_sequence_t *bases;
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *decorator_list;
        } ClassDef;

        struct {
            tinypy_ast_expression_t value;
        } Return;

        struct {
            tinypy_ast_sequence_t *targets;
        } Delete;

        struct {
            tinypy_ast_sequence_t *targets;
            tinypy_ast_expression_t value;
        } Assign;

        struct {
            tinypy_ast_expression_t target;
            tinypy_ast_binary_operator_e op;
            tinypy_ast_expression_t value;
        } AugAssign;

        struct {
            tinypy_ast_expression_t dest;
            tinypy_ast_sequence_t *values;
            tinypy_compiler_boolean_e nl;
        } Print;

        struct {
            tinypy_ast_expression_t target;
            tinypy_ast_expression_t iter;
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *orelse;
        } For;

        struct {
            tinypy_ast_expression_t test;
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *orelse;
        } While;

        struct {
            tinypy_ast_expression_t test;
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *orelse;
        } If;

        struct {
            tinypy_ast_expression_t context_expr;
            tinypy_ast_expression_t optional_vars;
            tinypy_ast_sequence_t *body;
        } With;

        struct {
            tinypy_ast_expression_t type;
            tinypy_ast_expression_t inst;
            tinypy_ast_expression_t tback;
        } Raise;

        struct {
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *handlers;
            tinypy_ast_sequence_t *orelse;
        } TryExcept;

        struct {
            tinypy_ast_sequence_t *body;
            tinypy_ast_sequence_t *finalbody;
        } TryFinally;

        struct {
            tinypy_ast_expression_t test;
            tinypy_ast_expression_t msg;
        } Assert;

        struct {
            tinypy_ast_sequence_t *names;
        } Import;

        struct {
            tinypy_ast_identifier_t module;
            tinypy_ast_sequence_t *names;
            int32_t level;
        } ImportFrom;

        struct {
            tinypy_ast_expression_t body;
            tinypy_ast_expression_t globals;
            tinypy_ast_expression_t locals;
        } Exec;

        struct {
            tinypy_ast_sequence_t *names;
        } Global;

        struct {
            tinypy_ast_expression_t value;
        } Expr;

    } v;
    int32_t lineno;
    int32_t col_offset;
};

typedef enum tinypy_ast_expression_kind_e {
    TINYPY_AST_KIND_BOOL_OP = 1,
    TINYPY_AST_KIND_BIN_OP = 2,
    TINYPY_AST_KIND_UNARY_OP = 3,
    TINYPY_AST_KIND_LAMBDA = 4,
    TINYPY_AST_KIND_IF_EXP = 5,
    TINYPY_AST_KIND_DICT = 6,
    TINYPY_AST_KIND_SET = 7,
    TINYPY_AST_KIND_LIST_COMP = 8,
    TINYPY_AST_KIND_SET_COMP = 9,
    TINYPY_AST_KIND_DICT_COMP = 10,
    TINYPY_AST_KIND_GENERATOR_EXP = 11,
    TINYPY_AST_KIND_YIELD = 12,
    TINYPY_AST_KIND_COMPARE = 13,
    TINYPY_AST_KIND_CALL = 14,
    TINYPY_AST_KIND_REPR = 15,
    TINYPY_AST_KIND_NUM = 16,
    TINYPY_AST_KIND_STR = 17,
    TINYPY_AST_KIND_ATTRIBUTE = 18,
    TINYPY_AST_KIND_SUBSCRIPT = 19,
    TINYPY_AST_KIND_NAME = 20,
    TINYPY_AST_KIND_LIST = 21,
    TINYPY_AST_KIND_TUPLE = 22
} tinypy_ast_expression_kind_e;
struct tinypy_ast_expression_s {
    tinypy_ast_expression_kind_e kind;
    union {
        struct {
            tinypy_ast_boolean_operator_e op;
            tinypy_ast_sequence_t *values;
        } BoolOp;

        struct {
            tinypy_ast_expression_t left;
            tinypy_ast_binary_operator_e op;
            tinypy_ast_expression_t right;
        } BinOp;

        struct {
            tinypy_ast_unary_operator_e op;
            tinypy_ast_expression_t operand;
        } UnaryOp;

        struct {
            tinypy_ast_arguments_t args;
            tinypy_ast_expression_t body;
        } Lambda;

        struct {
            tinypy_ast_expression_t test;
            tinypy_ast_expression_t body;
            tinypy_ast_expression_t orelse;
        } IfExp;

        struct {
            tinypy_ast_sequence_t *keys;
            tinypy_ast_sequence_t *values;
        } Dict;

        struct {
            tinypy_ast_sequence_t *elts;
        } Set;

        struct {
            tinypy_ast_expression_t elt;
            tinypy_ast_sequence_t *generators;
        } ListComp;

        struct {
            tinypy_ast_expression_t elt;
            tinypy_ast_sequence_t *generators;
        } SetComp;

        struct {
            tinypy_ast_expression_t key;
            tinypy_ast_expression_t value;
            tinypy_ast_sequence_t *generators;
        } DictComp;

        struct {
            tinypy_ast_expression_t elt;
            tinypy_ast_sequence_t *generators;
        } GeneratorExp;

        struct {
            tinypy_ast_expression_t value;
        } Yield;

        struct {
            tinypy_ast_expression_t left;
            tinypy_ast_integer_sequence_t *ops;
            tinypy_ast_sequence_t *comparators;
        } Compare;

        struct {
            tinypy_ast_expression_t func;
            tinypy_ast_sequence_t *args;
            tinypy_ast_sequence_t *keywords;
            tinypy_ast_expression_t starargs;
            tinypy_ast_expression_t kwargs;
        } Call;

        struct {
            tinypy_ast_expression_t value;
        } Repr;

        struct {
            tinypy_ast_literal_t n;
        } Num;

        struct {
            tinypy_ast_string_t s;
        } Str;

        struct {
            tinypy_ast_expression_t value;
            tinypy_ast_identifier_t attr;
            tinypy_ast_expression_context_e ctx;
        } Attribute;

        struct {
            tinypy_ast_expression_t value;
            tinypy_ast_slice_t slice;
            tinypy_ast_expression_context_e ctx;
        } Subscript;

        struct {
            tinypy_ast_identifier_t id;
            tinypy_ast_expression_context_e ctx;
        } Name;

        struct {
            tinypy_ast_sequence_t *elts;
            tinypy_ast_expression_context_e ctx;
        } List;

        struct {
            tinypy_ast_sequence_t *elts;
            tinypy_ast_expression_context_e ctx;
        } Tuple;

    } v;
    int32_t lineno;
    int32_t col_offset;
};

typedef enum tinypy_ast_slice_kind_e {
    TINYPY_AST_KIND_ELLIPSIS = 1,
    TINYPY_AST_KIND_SLICE = 2,
    TINYPY_AST_KIND_EXT_SLICE = 3,
    TINYPY_AST_KIND_INDEX = 4
} tinypy_ast_slice_kind_e;
struct tinypy_ast_slice_s {
    tinypy_ast_slice_kind_e kind;
    union {
        struct {
            tinypy_ast_expression_t lower;
            tinypy_ast_expression_t upper;
            tinypy_ast_expression_t step;
        } Slice;

        struct {
            tinypy_ast_sequence_t *dims;
        } ExtSlice;

        struct {
            tinypy_ast_expression_t value;
        } Index;

    } v;
};

struct tinypy_ast_comprehension_s {
    tinypy_ast_expression_t target;
    tinypy_ast_expression_t iter;
    tinypy_ast_sequence_t *ifs;
};

typedef enum tinypy_ast_exception_handler_kind_e {
    TINYPY_AST_KIND_EXCEPT_HANDLER = 1
} tinypy_ast_exception_handler_kind_e;
struct tinypy_ast_exception_handler_s {
    tinypy_ast_exception_handler_kind_e kind;
    union {
        struct {
            tinypy_ast_expression_t type;
            tinypy_ast_expression_t name;
            tinypy_ast_sequence_t *body;
        } ExceptHandler;

    } v;
    int32_t lineno;
    int32_t col_offset;
};

struct tinypy_ast_arguments_s {
    tinypy_ast_sequence_t *args;
    tinypy_ast_identifier_t vararg;
    tinypy_ast_identifier_t kwarg;
    tinypy_ast_sequence_t *defaults;
};

struct tinypy_ast_keyword_s {
    tinypy_ast_identifier_t arg;
    tinypy_ast_expression_t value;
};

struct tinypy_ast_alias_s {
    tinypy_ast_identifier_t name;
    tinypy_ast_identifier_t asname;
};

tinypy_ast_module_t __tinypy_ast_module(tinypy_ast_sequence_t *body, tinypy_compile_ctx_t *arena);
tinypy_ast_module_t __tinypy_ast_interactive(tinypy_ast_sequence_t *body, tinypy_compile_ctx_t *arena);
tinypy_ast_module_t __tinypy_ast_expression(tinypy_ast_expression_t body, tinypy_compile_ctx_t *arena);
tinypy_ast_module_t __tinypy_ast_suite(tinypy_ast_sequence_t *body, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_function_def(tinypy_ast_identifier_t name, tinypy_ast_arguments_t args, tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *decorator_list, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_class_def(tinypy_ast_identifier_t name, tinypy_ast_sequence_t *bases, tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *decorator_list, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_return(tinypy_ast_expression_t value, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_delete(tinypy_ast_sequence_t *targets, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_assign(tinypy_ast_sequence_t *targets, tinypy_ast_expression_t value, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_aug_assign(tinypy_ast_expression_t target, tinypy_ast_binary_operator_e op, tinypy_ast_expression_t value, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_print(tinypy_ast_expression_t dest, tinypy_ast_sequence_t *values, tinypy_compiler_boolean_e nl, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_for(tinypy_ast_expression_t target, tinypy_ast_expression_t iter, tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *orelse, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_while(tinypy_ast_expression_t test, tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *orelse, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_if(tinypy_ast_expression_t test, tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *orelse, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_with(tinypy_ast_expression_t context_expr, tinypy_ast_expression_t optional_vars, tinypy_ast_sequence_t *body, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_raise(tinypy_ast_expression_t type, tinypy_ast_expression_t inst, tinypy_ast_expression_t tback, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_try_except(tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *handlers, tinypy_ast_sequence_t *orelse, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_try_finally(tinypy_ast_sequence_t *body, tinypy_ast_sequence_t *finalbody, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_assert(tinypy_ast_expression_t test, tinypy_ast_expression_t msg, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_import(tinypy_ast_sequence_t *names, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_import_from(tinypy_ast_identifier_t module, tinypy_ast_sequence_t *names, int32_t level, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_exec(tinypy_ast_expression_t body, tinypy_ast_expression_t globals, tinypy_ast_expression_t locals, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_global(tinypy_ast_sequence_t *names, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_expr(tinypy_ast_expression_t value, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_pass(int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_break(int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_statement_t __tinypy_ast_continue(int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_bool_op(tinypy_ast_boolean_operator_e op, tinypy_ast_sequence_t *values, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_bin_op(tinypy_ast_expression_t left, tinypy_ast_binary_operator_e op, tinypy_ast_expression_t right, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_unary_op(tinypy_ast_unary_operator_e op, tinypy_ast_expression_t operand, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_lambda(tinypy_ast_arguments_t args, tinypy_ast_expression_t body, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_if_exp(tinypy_ast_expression_t test, tinypy_ast_expression_t body, tinypy_ast_expression_t orelse, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_dict(tinypy_ast_sequence_t *keys, tinypy_ast_sequence_t *values, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_set(tinypy_ast_sequence_t *elts, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_list_comp(tinypy_ast_expression_t elt, tinypy_ast_sequence_t *generators, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_set_comp(tinypy_ast_expression_t elt, tinypy_ast_sequence_t *generators, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_dict_comp(tinypy_ast_expression_t key, tinypy_ast_expression_t value, tinypy_ast_sequence_t *generators, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_generator_exp(tinypy_ast_expression_t elt, tinypy_ast_sequence_t *generators, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_yield(tinypy_ast_expression_t value, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_compare(tinypy_ast_expression_t left, tinypy_ast_integer_sequence_t *ops, tinypy_ast_sequence_t *comparators, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_call(tinypy_ast_expression_t func, tinypy_ast_sequence_t *args, tinypy_ast_sequence_t *keywords, tinypy_ast_expression_t starargs, tinypy_ast_expression_t kwargs, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_repr(tinypy_ast_expression_t value, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_num(tinypy_ast_literal_t n, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_str(tinypy_ast_string_t s, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_attribute(tinypy_ast_expression_t value, tinypy_ast_identifier_t attr, tinypy_ast_expression_context_e ctx, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_subscript(tinypy_ast_expression_t value, tinypy_ast_slice_t slice, tinypy_ast_expression_context_e ctx, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_name(tinypy_ast_identifier_t id, tinypy_ast_expression_context_e ctx, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_list(tinypy_ast_sequence_t *elts, tinypy_ast_expression_context_e ctx, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_expression_t __tinypy_ast_tuple(tinypy_ast_sequence_t *elts, tinypy_ast_expression_context_e ctx, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_slice_t __tinypy_ast_ellipsis(tinypy_compile_ctx_t *arena);
tinypy_ast_slice_t __tinypy_ast_slice(tinypy_ast_expression_t lower, tinypy_ast_expression_t upper, tinypy_ast_expression_t step, tinypy_compile_ctx_t *arena);
tinypy_ast_slice_t __tinypy_ast_ext_slice(tinypy_ast_sequence_t *dims, tinypy_compile_ctx_t *arena);
tinypy_ast_slice_t __tinypy_ast_index(tinypy_ast_expression_t value, tinypy_compile_ctx_t *arena);
tinypy_ast_comprehension_t __tinypy_ast_comprehension(tinypy_ast_expression_t target, tinypy_ast_expression_t iter, tinypy_ast_sequence_t *ifs, tinypy_compile_ctx_t *arena);
tinypy_ast_exception_handler_t __tinypy_ast_except_handler(tinypy_ast_expression_t type, tinypy_ast_expression_t name, tinypy_ast_sequence_t *body, int32_t lineno, int32_t col_offset, tinypy_compile_ctx_t *arena);
tinypy_ast_arguments_t __tinypy_ast_arguments(tinypy_ast_sequence_t *args, tinypy_ast_identifier_t vararg, tinypy_ast_identifier_t kwarg, tinypy_ast_sequence_t *defaults, tinypy_compile_ctx_t *arena);
tinypy_ast_keyword_t __tinypy_ast_keyword(tinypy_ast_identifier_t arg, tinypy_ast_expression_t value, tinypy_compile_ctx_t *arena);
tinypy_ast_alias_t __tinypy_ast_alias(tinypy_ast_identifier_t name, tinypy_ast_identifier_t asname, tinypy_compile_ctx_t *arena);

#endif
