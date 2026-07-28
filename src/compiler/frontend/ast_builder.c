/*
 * This file includes functions to transform a concrete syntax tree (CST) to
 * an abstract syntax tree (AST).  The main function is __tinypy_ast_build().
 *
 */
#include "value_ops.h"
#include "ast_nodes.h"
#include "grammar.h"
#include "cst.h"
#include "ast_builder.h"
#include "token.h"
#include "source_parser.h"
#include "grammar_symbols.h"

/* Data structure used internally */
typedef struct tinypy_ast_builder_t {
    char *c_encoding;              /* source encoding */
    tinypy_bool_t c_future_unicode;          /* __future__ unicode literals flag */
    tinypy_compile_ctx_t *c_arena; /* arena for allocating memeory */
    const char *c_filename;        /* filename */
} tinypy_ast_builder_t;

static tinypy_ast_sequence_t *__seq_for_testlist(tinypy_ast_builder_t *, const tinypy_cst_node_t *);
static tinypy_ast_expression_t __ast_for_expr(tinypy_ast_builder_t *, const tinypy_cst_node_t *);
static tinypy_ast_statement_t __ast_for_stmt(tinypy_ast_builder_t *, const tinypy_cst_node_t *);
static tinypy_ast_sequence_t *__ast_for_suite(tinypy_ast_builder_t *, const tinypy_cst_node_t *);
static tinypy_ast_sequence_t *__ast_for_exprlist(tinypy_ast_builder_t *, const tinypy_cst_node_t *, tinypy_ast_expression_context_e);
static tinypy_ast_expression_t __ast_for_testlist(tinypy_ast_builder_t *, const tinypy_cst_node_t *);
static tinypy_ast_statement_t __ast_for_classdef(tinypy_ast_builder_t *, const tinypy_cst_node_t *, tinypy_ast_sequence_t *);
static tinypy_ast_expression_t __ast_for_testlist_comp(tinypy_ast_builder_t *, const tinypy_cst_node_t *);

/* Note different signature for __ast_for_call */
static tinypy_ast_expression_t __ast_for_call(tinypy_ast_builder_t *, const tinypy_cst_node_t *, tinypy_ast_expression_t);

static tinypy_value_t *__parsenumber(tinypy_ast_builder_t *, const char *);
static tinypy_value_t *__parsestr(tinypy_ast_builder_t *, const tinypy_cst_node_t *n, const char *);
static tinypy_value_t *__parsestrplus(tinypy_ast_builder_t *, const tinypy_cst_node_t *n);

#ifndef LINENO
#define TINYPY_AST_LINE_NUMBER(n) ((n)->line_number)
#endif

#define TINYPY_AST_BUILDER_COMPREHENSION_GENERATOR 0
#define TINYPY_AST_BUILDER_COMPREHENSION_SET 1

//////////////////////////////////////////////////////////////////////////
static tinypy_ast_identifier_t __new_identifier(const char *n, tinypy_compile_ctx_t *arena) {
    unsigned long size = strlen(n);
    tinypy_value_t *id = tinypy_string_from_bytes(arena->vm, n, size);
    if (id != NULL) {
        tinypy_internal_string_set_interned(id, 1);
    }
    if (id != NULL && TINYPY_COMPILER_ARENA_ADD_VALUE(arena, id) != 0) {
        TINYPY_DECREF(id);
        return NULL;
    }
    return id;
}

#define TINYPY_AST_NEW_IDENTIFIER(n) __new_identifier(TINYPY_CST_TEXT(n), c->c_arena)

/* This routine provides an invalid object for the syntax error.
   The outermost routine must unpack this error and create the
   proper object.  We do this so that we don't have to pass
   the filename to everything function.

   XXX Maybe we should just pass the filename...
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __ast_error(const tinypy_cst_node_t *n, const char *errstr) {
    tinypy_internal_compiler_error(n->context, TINYPY_ERROR_SYNTAX, errstr, TINYPY_AST_LINE_NUMBER(n), n->column_offset + 1, n->context->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __ast_error_finish(const char *filename) {
    (void)filename;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __forbidden_check(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, const char *x) {
    (void)c;
    if (!strcmp(x, "None")) {
        tinypy_bool_t return_value_1 = __ast_error(n, "cannot assign to None");
        return return_value_1;
    }
    if (!strcmp(x, "__debug__")) {
        tinypy_bool_t return_value_2 = __ast_error(n, "cannot assign to __debug__");
        return return_value_2;
    }
    return TINYPY_TRUE;
}

/* __num_stmts() returns number of contained statements.

   Use this routine to determine how big a sequence is needed for
   the statements in a parse tree.  Its raison d'etre is this bit of
   grammar:

   TINYPY_GRAMMAR_STMT: TINYPY_GRAMMAR_SIMPLE_STMT | TINYPY_GRAMMAR_COMPOUND_STMT
   TINYPY_GRAMMAR_SIMPLE_STMT: TINYPY_GRAMMAR_SMALL_STMT (';' TINYPY_GRAMMAR_SMALL_STMT)* [';'] TINYPY_TOKEN_NEWLINE

   A TINYPY_GRAMMAR_SIMPLE_STMT can contain multiple TINYPY_GRAMMAR_SMALL_STMT elements joined
   by semicolons.  If the arg is a TINYPY_GRAMMAR_SIMPLE_STMT, the number of
   TINYPY_GRAMMAR_SMALL_STMT elements is returned.
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __num_stmts(const tinypy_cst_node_t *n) {
    int32_t function_result;
    int32_t i, l;
    tinypy_cst_node_t *ch;

    switch (TINYPY_CST_TYPE(n)) {
    case TINYPY_GRAMMAR_SINGLE_INPUT:
        if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_TOKEN_NEWLINE) {
            return 0;
        }
        else {
            int32_t return_value_1 = __num_stmts(TINYPY_CST_CHILD(n, 0));
            return return_value_1;
        }
    case TINYPY_GRAMMAR_FILE_INPUT:
        l = 0;
        for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i++) {
            ch = TINYPY_CST_CHILD(n, i);
            if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_STMT) {
                l += __num_stmts(ch);
            }
        }
        return l;
    case TINYPY_GRAMMAR_STMT:
        function_result = __num_stmts(TINYPY_CST_CHILD(n, 0));
        return function_result;
    case TINYPY_GRAMMAR_COMPOUND_STMT:
        return 1;
    case TINYPY_GRAMMAR_SIMPLE_STMT:
        function_result = TINYPY_CST_CHILD_COUNT(n) / 2;
        return function_result; /* Divide by 2 to remove count of semi-colons */
    case TINYPY_GRAMMAR_SUITE:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            int32_t return_value_2 = __num_stmts(TINYPY_CST_CHILD(n, 0));
            return return_value_2;
        }
        else {
            l = 0;
            for (i = 2; i < (TINYPY_CST_CHILD_COUNT(n) - 1); i++) {
                l += __num_stmts(TINYPY_CST_CHILD(n, i));
            }
            return l;
        }
    default:
        return 0;
    }
}

/* Transform the CST rooted at tinypy_cst_node_t * to the appropriate AST
 */

//////////////////////////////////////////////////////////////////////////
tinypy_ast_module_t __tinypy_ast_build(const tinypy_cst_node_t *n, tinypy_compiler_flags_t *flags, const char *filename, tinypy_compile_ctx_t *arena) {
    int32_t i, j, k, num;
    tinypy_ast_sequence_t *stmts = NULL;
    tinypy_ast_statement_t s;
    tinypy_cst_node_t *ch;
    tinypy_ast_builder_t c;

    if (flags && flags->flags & TINYPY_COMPILER_FLAG_SOURCE_IS_UTF8) {
        c.c_encoding = "utf-8";
        if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_ENCODING_DECL) {
            __ast_error(n, "encoding declaration in Unicode string");
            goto error;
        }
    }
    else if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_ENCODING_DECL) {
        c.c_encoding = TINYPY_CST_TEXT(n);
        n = TINYPY_CST_CHILD(n, 0);
    }
    else {
        c.c_encoding = NULL;
    }
    c.c_future_unicode = flags && flags->flags & TINYPY_CODE_FUTURE_UNICODE_LITERALS;
    c.c_arena = arena;
    c.c_filename = filename;

    k = 0;
    switch (TINYPY_CST_TYPE(n)) {
    case TINYPY_GRAMMAR_FILE_INPUT:
        stmts = TINYPY_AST_SEQUENCE_NEW(__num_stmts(n), arena);
        if (!stmts) {
            return NULL;
        }
        for (i = 0; i < TINYPY_CST_CHILD_COUNT(n) - 1; i++) {
            ch = TINYPY_CST_CHILD(n, i);
            if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_NEWLINE) {
                continue;
            }
            num = __num_stmts(ch);
            if (num == 1) {
                s = __ast_for_stmt(&c, ch);
                if (!s) {
                    goto error;
                }
                TINYPY_AST_SEQUENCE_SET(stmts, k++, s);
            }
            else {
                ch = TINYPY_CST_CHILD(ch, 0);
                for (j = 0; j < num; j++) {
                    s = __ast_for_stmt(&c, TINYPY_CST_CHILD(ch, j * 2));
                    if (!s) {
                        goto error;
                    }
                    TINYPY_AST_SEQUENCE_SET(stmts, k++, s);
                }
            }
        }
        tinypy_ast_module_t return_value_1 = __tinypy_ast_module(stmts, arena);
        return return_value_1;
    case TINYPY_GRAMMAR_EVAL_INPUT: {
        tinypy_ast_expression_t testlist_ast;

        /* XXX Why not TINYPY_GRAMMAR_COMP_FOR here? */
        testlist_ast = __ast_for_testlist(&c, TINYPY_CST_CHILD(n, 0));
        if (!testlist_ast) {
            goto error;
        }
        tinypy_ast_module_t return_value_2 = __tinypy_ast_expression(testlist_ast, arena);
        return return_value_2;
    }
    case TINYPY_GRAMMAR_SINGLE_INPUT:
        if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_TOKEN_NEWLINE) {
            stmts = TINYPY_AST_SEQUENCE_NEW(1, arena);
            if (!stmts) {
                goto error;
            }
            TINYPY_AST_SEQUENCE_SET(stmts, 0, __tinypy_ast_pass(n->line_number, n->column_offset, arena));
            if (!TINYPY_AST_SEQUENCE_GET(stmts, 0)) {
                goto error;
            }
            tinypy_ast_module_t return_value_3 = __tinypy_ast_interactive(stmts, arena);
            return return_value_3;
        }
        else {
            n = TINYPY_CST_CHILD(n, 0);
            num = __num_stmts(n);
            stmts = TINYPY_AST_SEQUENCE_NEW(num, arena);
            if (!stmts) {
                goto error;
            }
            if (num == 1) {
                s = __ast_for_stmt(&c, n);
                if (!s) {
                    goto error;
                }
                TINYPY_AST_SEQUENCE_SET(stmts, 0, s);
            }
            else {
                /* Only a TINYPY_GRAMMAR_SIMPLE_STMT can contain multiple statements. */
                for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
                    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, i)) == TINYPY_TOKEN_NEWLINE) {
                        break;
                    }
                    s = __ast_for_stmt(&c, TINYPY_CST_CHILD(n, i));
                    if (!s) {
                        goto error;
                    }
                    TINYPY_AST_SEQUENCE_SET(stmts, i / 2, s);
                }
            }

            tinypy_ast_module_t return_value_4 = __tinypy_ast_interactive(stmts, arena);
            return return_value_4;
        }
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "invalid tinypy_cst_node_t %d for __tinypy_ast_build", TINYPY_CST_TYPE(n));
        goto error;
    }
error:
    __ast_error_finish(filename);
    return NULL;
}

/* Return the AST repr. of the operator represented as syntax (|, ^, etc.)
 */

//////////////////////////////////////////////////////////////////////////
static tinypy_ast_binary_operator_e __get_operator(const tinypy_cst_node_t *n) {
    switch (TINYPY_CST_TYPE(n)) {
    case TINYPY_TOKEN_VERTICAL_BAR:
        return TINYPY_AST_BINARY_BIT_OR;
    case TINYPY_TOKEN_CIRCUMFLEX:
        return TINYPY_AST_BINARY_BIT_XOR;
    case TINYPY_TOKEN_AMPERSAND:
        return TINYPY_AST_BINARY_BIT_AND;
    case TINYPY_TOKEN_LEFT_SHIFT:
        return TINYPY_AST_BINARY_LEFT_SHIFT;
    case TINYPY_TOKEN_RIGHT_SHIFT:
        return TINYPY_AST_BINARY_RIGHT_SHIFT;
    case TINYPY_TOKEN_PLUS:
        return TINYPY_AST_BINARY_ADD;
    case TINYPY_TOKEN_MINUS:
        return TINYPY_AST_BINARY_SUBTRACT;
    case TINYPY_TOKEN_STAR:
        return TINYPY_AST_BINARY_MULTIPLY;
    case TINYPY_TOKEN_SLASH:
        return TINYPY_AST_BINARY_DIVIDE;
    case TINYPY_TOKEN_DOUBLE_SLASH:
        return TINYPY_AST_BINARY_FLOOR_DIVIDE;
    case TINYPY_TOKEN_PERCENT:
        return TINYPY_AST_BINARY_MODULO;
    default:
        return (tinypy_ast_binary_operator_e)0;
    }
}

/* Set the context ctx for tinypy_ast_expression_t e, recursively traversing e.

   Only sets context for TINYPY_GRAMMAR_EXPR kinds that "can appear in assignment context"
   (according to tinypy's Python 2.7 AST schema). For other expression kinds, it sets
   an appropriate syntax error and returns TINYPY_COMPILER_FALSE.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __set_context(tinypy_ast_builder_t *c, tinypy_ast_expression_t e, tinypy_ast_expression_context_e ctx, const tinypy_cst_node_t *n) {
    tinypy_ast_sequence_t *s = NULL;
    /* If a particular expression type can't be used for assign / delete,
       set expr_name to its name and an error message will be generated.
    */
    const char *expr_name = NULL;

    /* The ast defines augmented store and load contexts, but the
       implementation here doesn't actually use them.  The code may be
       a little more complex than necessary as a result.  It also means
       that expressions in an augmented assignment have a TINYPY_AST_CONTEXT_STORE context.
       Consider restructuring so that augmented assignment uses
       __set_context(), too.
    */

    switch (e->kind) {
    case TINYPY_AST_KIND_ATTRIBUTE:
        if (ctx == TINYPY_AST_CONTEXT_STORE && !__forbidden_check(c, n,
                                                                  TINYPY_COMPILER_BYTES_AS_STRING(e->v.Attribute.attr))) {
            return TINYPY_FALSE;
        }
        e->v.Attribute.ctx = ctx;
        break;
    case TINYPY_AST_KIND_SUBSCRIPT:
        e->v.Subscript.ctx = ctx;
        break;
    case TINYPY_AST_KIND_NAME:
        if (ctx == TINYPY_AST_CONTEXT_STORE && !__forbidden_check(c, n,
                                                                  TINYPY_COMPILER_BYTES_AS_STRING(e->v.Name.id))) {
            return TINYPY_FALSE;
        }
        e->v.Name.ctx = ctx;
        break;
    case TINYPY_AST_KIND_LIST:
        e->v.List.ctx = ctx;
        s = e->v.List.elts;
        break;
    case TINYPY_AST_KIND_TUPLE:
        if (TINYPY_AST_SEQUENCE_LENGTH(e->v.Tuple.elts)) {
            e->v.Tuple.ctx = ctx;
            s = e->v.Tuple.elts;
        }
        else {
            expr_name = "()";
        }
        break;
    case TINYPY_AST_KIND_LAMBDA:
        expr_name = "lambda";
        break;
    case TINYPY_AST_KIND_CALL:
        expr_name = "function call";
        break;
    case TINYPY_AST_KIND_BOOL_OP:
    case TINYPY_AST_KIND_BIN_OP:
    case TINYPY_AST_KIND_UNARY_OP:
        expr_name = "operator";
        break;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        expr_name = "generator expression";
        break;
    case TINYPY_AST_KIND_YIELD:
        expr_name = "yield expression";
        break;
    case TINYPY_AST_KIND_LIST_COMP:
        expr_name = "list comprehension";
        break;
    case TINYPY_AST_KIND_SET_COMP:
        expr_name = "set comprehension";
        break;
    case TINYPY_AST_KIND_DICT_COMP:
        expr_name = "dict comprehension";
        break;
    case TINYPY_AST_KIND_DICT:
    case TINYPY_AST_KIND_SET:
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
        expr_name = "literal";
        break;
    case TINYPY_AST_KIND_COMPARE:
        expr_name = "TINYPY_GRAMMAR_COMPARISON";
        break;
    case TINYPY_AST_KIND_REPR:
        expr_name = "repr";
        break;
    case TINYPY_AST_KIND_IF_EXP:
        expr_name = "conditional expression";
        break;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "unexpected expression in assignment %d (line %d)",
                                   e->kind, e->lineno);
        return TINYPY_FALSE;
    }
    /* Check for error string set by switch */
    if (expr_name) {
        static const char assign_prefix[] = "can't assign to ";
        static const char delete_prefix[] = "can't delete ";
        const char *prefix = ctx == TINYPY_AST_CONTEXT_STORE ? assign_prefix : delete_prefix;
        const char *parts[] = {prefix, expr_name};
        size_t part_sizes[] = {strlen(prefix), strlen(expr_name)};

        tinypy_internal_compiler_error_parts(n->context, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), TINYPY_AST_LINE_NUMBER(n), n->column_offset + 1);
        return TINYPY_FALSE;
    }

    /* If the LHS is a list or tuple, we need to set the assignment
       context for all the contained elements.
    */
    if (s) {
        int32_t i;

        for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(s); i++) {
            if (!__set_context(c, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(s, i), ctx, n)) {
                return TINYPY_FALSE;
            }
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_binary_operator_e __ast_for_augassign(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    (void)c;
    n = TINYPY_CST_CHILD(n, 0);
    switch (TINYPY_CST_TEXT(n)[0]) {
    case '+':
        return TINYPY_AST_BINARY_ADD;
    case '-':
        return TINYPY_AST_BINARY_SUBTRACT;
    case '/':
        if (TINYPY_CST_TEXT(n)[1] == '/') {
            return TINYPY_AST_BINARY_FLOOR_DIVIDE;
        }
        else {
            return TINYPY_AST_BINARY_DIVIDE;
        }
    case '%':
        return TINYPY_AST_BINARY_MODULO;
    case '<':
        return TINYPY_AST_BINARY_LEFT_SHIFT;
    case '>':
        return TINYPY_AST_BINARY_RIGHT_SHIFT;
    case '&':
        return TINYPY_AST_BINARY_BIT_AND;
    case '^':
        return TINYPY_AST_BINARY_BIT_XOR;
    case '|':
        return TINYPY_AST_BINARY_BIT_OR;
    case '*':
        if (TINYPY_CST_TEXT(n)[1] == '*') {
            return TINYPY_AST_BINARY_POWER;
        }
        else {
            return TINYPY_AST_BINARY_MULTIPLY;
        }
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "invalid TINYPY_GRAMMAR_AUGASSIGN: %s", TINYPY_CST_TEXT(n));
        return (tinypy_ast_binary_operator_e)0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_compare_operator_e __ast_for_comp_op(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    (void)c;
    /* TINYPY_GRAMMAR_COMP_OP: '<'|'>'|'=='|'>='|'<='|'<>'|'!='|'in'|'not' 'in'|'is'
               |'is' 'not'
    */
    if (TINYPY_CST_CHILD_COUNT(n) == 1) {
        n = TINYPY_CST_CHILD(n, 0);
        switch (TINYPY_CST_TYPE(n)) {
        case TINYPY_TOKEN_LESS:
            return TINYPY_AST_COMPARE_LESS;
        case TINYPY_TOKEN_GREATER:
            return TINYPY_AST_COMPARE_GREATER;
        case TINYPY_TOKEN_EQUAL_EQUAL: /* == */
            return TINYPY_AST_COMPARE_EQUAL;
        case TINYPY_TOKEN_LESS_EQUAL:
            return TINYPY_AST_COMPARE_LESS_EQUAL;
        case TINYPY_TOKEN_GREATER_EQUAL:
            return TINYPY_AST_COMPARE_GREATER_EQUAL;
        case TINYPY_TOKEN_NOT_EQUAL:
            return TINYPY_AST_COMPARE_NOT_EQUAL;
        case TINYPY_TOKEN_NAME:
            if (strcmp(TINYPY_CST_TEXT(n), "in") == 0) {
                return TINYPY_AST_COMPARE_IN;
            }
            if (strcmp(TINYPY_CST_TEXT(n), "is") == 0) {
                return TINYPY_AST_COMPARE_IS;
            }
        default:
            break;
        }
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "invalid TINYPY_GRAMMAR_COMP_OP: %s",
                                   TINYPY_CST_TEXT(n));
        return (tinypy_ast_compare_operator_e)0;
    }
    else if (TINYPY_CST_CHILD_COUNT(n) == 2) {
        /* handle "not in" and "is not" */
        switch (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0))) {
        case TINYPY_TOKEN_NAME:
            if (strcmp(TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 1)), "in") == 0) {
                return TINYPY_AST_COMPARE_NOT_IN;
            }
            if (strcmp(TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 0)), "is") == 0) {
                return TINYPY_AST_COMPARE_IS_NOT;
            }
        default:
            break;
        }
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "invalid TINYPY_GRAMMAR_COMP_OP: %s %s",
                                   TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 0)), TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 1)));
        return (tinypy_ast_compare_operator_e)0;
    }
    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "invalid TINYPY_GRAMMAR_COMP_OP: has %d children",
                               TINYPY_CST_CHILD_COUNT(n));
    return (tinypy_ast_compare_operator_e)0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__seq_for_testlist(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_TESTLIST: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* [','] */
    tinypy_ast_expression_t expression;
    int32_t i;

    tinypy_ast_sequence_t *seq = TINYPY_AST_SEQUENCE_NEW((TINYPY_CST_CHILD_COUNT(n) + 1) / 2, c->c_arena);
    if (!seq) {
        return NULL;
    }

    for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {

        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, i));
        if (!expression) {
            return NULL;
        }

        TINYPY_AST_SEQUENCE_SET(seq, i / 2, expression);
    }
    return seq;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __compiler_complex_args(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    int32_t i, len = (TINYPY_CST_CHILD_COUNT(n) + 1) / 2;
    tinypy_ast_expression_t result;
    tinypy_ast_sequence_t *args = TINYPY_AST_SEQUENCE_NEW(len, c->c_arena);
    if (!args) {
        return NULL;
    }

    /* TINYPY_GRAMMAR_FPDEF: TINYPY_TOKEN_NAME | '(' TINYPY_GRAMMAR_FPLIST ')'
       TINYPY_GRAMMAR_FPLIST: TINYPY_GRAMMAR_FPDEF (',' TINYPY_GRAMMAR_FPDEF)* [',']
    */
    for (i = 0; i < len; i++) {
        tinypy_value_t *arg_id;
        const tinypy_cst_node_t *fpdef_node = TINYPY_CST_CHILD(n, 2 * i);
        const tinypy_cst_node_t *child;
        tinypy_ast_expression_t arg;
    set_name:
        /* fpdef_node is either a TINYPY_TOKEN_NAME or an TINYPY_GRAMMAR_FPLIST */
        child = TINYPY_CST_CHILD(fpdef_node, 0);
        if (TINYPY_CST_TYPE(child) == TINYPY_TOKEN_NAME) {
            if (!__forbidden_check(c, n, TINYPY_CST_TEXT(child))) {
                return NULL;
            }
            arg_id = TINYPY_AST_NEW_IDENTIFIER(child);
            if (!arg_id) {
                return NULL;
            }
            arg = __tinypy_ast_name(arg_id, TINYPY_AST_CONTEXT_STORE, TINYPY_AST_LINE_NUMBER(child), child->column_offset,
                                    c->c_arena);
        }
        else {
            /* fpdef_node[0] is not a name, so it must be '(', get CHILD[1] */
            child = TINYPY_CST_CHILD(fpdef_node, 1);
            /* NCH == 1 means we have (x), we need to elide the extra parens */
            if (TINYPY_CST_CHILD_COUNT(child) == 1) {
                fpdef_node = TINYPY_CST_CHILD(child, 0);
                goto set_name;
            }
            arg = __compiler_complex_args(c, child);
        }
        TINYPY_AST_SEQUENCE_SET(args, i, arg);
    }

    result = __tinypy_ast_tuple(args, TINYPY_AST_CONTEXT_STORE, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    if (!__set_context(c, result, TINYPY_AST_CONTEXT_STORE, n)) {
        return NULL;
    }
    return result;
}

/* Create AST for TINYPY_GRAMMAR_ARGUMENT list. */

//////////////////////////////////////////////////////////////////////////
static tinypy_ast_arguments_t __ast_for_arguments(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_PARAMETERS: '(' [TINYPY_GRAMMAR_VARARGSLIST] ')'
       TINYPY_GRAMMAR_VARARGSLIST: (TINYPY_GRAMMAR_FPDEF ['=' TINYPY_GRAMMAR_TEST] ',')* ('*' TINYPY_TOKEN_NAME [',' '**' TINYPY_TOKEN_NAME]
            | '**' TINYPY_TOKEN_NAME) | TINYPY_GRAMMAR_FPDEF ['=' TINYPY_GRAMMAR_TEST] (',' TINYPY_GRAMMAR_FPDEF ['=' TINYPY_GRAMMAR_TEST])* [',']
    */
    int32_t i, j, k, n_args = 0, n_defaults = 0;
    tinypy_bool_t found_default = TINYPY_FALSE;
    tinypy_ast_sequence_t *args, *defaults;
    tinypy_ast_identifier_t vararg = NULL, kwarg = NULL;
    tinypy_cst_node_t *ch;

    if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_PARAMETERS) {
        if (TINYPY_CST_CHILD_COUNT(n) == 2) /* () as TINYPY_GRAMMAR_ARGUMENT list */ {
            tinypy_ast_arguments_t return_value_1 = __tinypy_ast_arguments(NULL, NULL, NULL, NULL, c->c_arena);
            return return_value_1;
        }
        n = TINYPY_CST_CHILD(n, 1);
    }

    /* first count the number of normal args & defaults */
    for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i++) {
        ch = TINYPY_CST_CHILD(n, i);
        if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_FPDEF) {
            n_args++;
        }
        if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_EQUAL) {
            n_defaults++;
        }
    }
    args = (n_args ? TINYPY_AST_SEQUENCE_NEW(n_args, c->c_arena) : NULL);
    if (!args && n_args) {
        return NULL;
    }
    defaults = (n_defaults ? TINYPY_AST_SEQUENCE_NEW(n_defaults, c->c_arena) : NULL);
    if (!defaults && n_defaults) {
        return NULL;
    }

    /* TINYPY_GRAMMAR_FPDEF: TINYPY_TOKEN_NAME | '(' TINYPY_GRAMMAR_FPLIST ')'
       TINYPY_GRAMMAR_FPLIST: TINYPY_GRAMMAR_FPDEF (',' TINYPY_GRAMMAR_FPDEF)* [',']
    */
    i = 0;
    j = 0; /* index for defaults */
    k = 0; /* index for args */
    while (i < TINYPY_CST_CHILD_COUNT(n)) {
        ch = TINYPY_CST_CHILD(n, i);
        switch (TINYPY_CST_TYPE(ch)) {
        case TINYPY_GRAMMAR_FPDEF: {
            int32_t complex_args = 0, parenthesized = 0;
        handle_fpdef:
            /* XXX Need to worry about checking if TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, i+1)) is
               anything other than TINYPY_TOKEN_EQUAL or a comma? */
            /* XXX Should TINYPY_CST_CHILD_COUNT(n) check be made a separate check? */
            if (i + 1 < TINYPY_CST_CHILD_COUNT(n) && TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, i + 1)) == TINYPY_TOKEN_EQUAL) {
                tinypy_ast_expression_t expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, i + 2));
                if (!expression) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(defaults, j++, expression);
                i += 2;
                found_default = 1;
            }
            else if (found_default) {
                /* def f((x)=4): pass should raise an error.
                   def f((x, (y))): pass will just incur the tuple unpacking warning. */
                if (parenthesized && !complex_args) {
                    __ast_error(n, "parenthesized arg with default");
                    return NULL;
                }
                __ast_error(n,
                            "non-default TINYPY_GRAMMAR_ARGUMENT follows default TINYPY_GRAMMAR_ARGUMENT");
                return NULL;
            }
            if (TINYPY_CST_CHILD_COUNT(ch) == 3) {
                ch = TINYPY_CST_CHILD(ch, 1);
                /* def foo((x)): is not complex, special case. */
                if (TINYPY_CST_CHILD_COUNT(ch) != 1) {
                    /* We have complex arguments, setup for unpacking. */
                    complex_args = 1;
                    TINYPY_AST_SEQUENCE_SET(args, k++, __compiler_complex_args(c, ch));
                    if (!TINYPY_AST_SEQUENCE_GET(args, k - 1)) {
                        return NULL;
                    }
                }
                else {
                    /* def foo((x)): setup for checking TINYPY_TOKEN_NAME below. */
                    /* Loop because there can be many parens and tuple
                       unpacking mixed in. */
                    parenthesized = 1;
                    ch = TINYPY_CST_CHILD(ch, 0);
                    goto handle_fpdef;
                }
            }
            if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 0)) == TINYPY_TOKEN_NAME) {
                tinypy_value_t *id;
                tinypy_ast_expression_t name;
                if (!__forbidden_check(c, n, TINYPY_CST_TEXT(TINYPY_CST_CHILD(ch, 0)))) {
                    return NULL;
                }
                id = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(ch, 0));
                if (!id) {
                    return NULL;
                }
                name = __tinypy_ast_name(id, TINYPY_AST_CONTEXT_PARAMETER, TINYPY_AST_LINE_NUMBER(ch), ch->column_offset,
                                         c->c_arena);
                if (!name) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(args, k++, name);
            }
            i += 2; /* the name and the comma */
            break;
        }
        case TINYPY_TOKEN_STAR:
            if (!__forbidden_check(c, TINYPY_CST_CHILD(n, i + 1), TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, i + 1)))) {
                return NULL;
            }
            vararg = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, i + 1));
            if (!vararg) {
                return NULL;
            }
            i += 3;
            break;
        case TINYPY_TOKEN_DOUBLE_STAR:
            if (!__forbidden_check(c, TINYPY_CST_CHILD(n, i + 1), TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, i + 1)))) {
                return NULL;
            }
            kwarg = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, i + 1));
            if (!kwarg) {
                return NULL;
            }
            i += 3;
            break;
        default:
            TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "unexpected tinypy_cst_node_t in TINYPY_GRAMMAR_VARARGSLIST: %d @ %d",
                                       TINYPY_CST_TYPE(ch), i);
            return NULL;
        }
    }

    tinypy_ast_arguments_t return_value_2 = __tinypy_ast_arguments(args, vararg, kwarg, defaults, c->c_arena);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_dotted_name(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t e;
    tinypy_ast_identifier_t id;
    int32_t lineno, col_offset;
    int32_t i;

    lineno = TINYPY_AST_LINE_NUMBER(n);
    col_offset = n->column_offset;

    id = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, 0));
    if (!id) {
        return NULL;
    }
    e = __tinypy_ast_name(id, TINYPY_AST_CONTEXT_LOAD, lineno, col_offset, c->c_arena);
    if (!e) {
        return NULL;
    }

    for (i = 2; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
        id = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, i));
        if (!id) {
            return NULL;
        }
        e = __tinypy_ast_attribute(e, id, TINYPY_AST_CONTEXT_LOAD, lineno, col_offset, c->c_arena);
        if (!e) {
            return NULL;
        }
    }

    return e;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_decorator(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_DECORATOR: '@' TINYPY_GRAMMAR_DOTTED_NAME [ '(' [TINYPY_GRAMMAR_ARGLIST] ')' ] TINYPY_TOKEN_NEWLINE */
    tinypy_ast_expression_t d = NULL;
    tinypy_ast_expression_t name_expr;

    name_expr = __ast_for_dotted_name(c, TINYPY_CST_CHILD(n, 1));
    if (!name_expr) {
        return NULL;
    }

    if (TINYPY_CST_CHILD_COUNT(n) == 3) { /* No arguments */
        d = name_expr;
        name_expr = NULL;
    }
    else if (TINYPY_CST_CHILD_COUNT(n) == 5) { /* Call with no arguments */
        d = __tinypy_ast_call(name_expr, NULL, NULL, NULL, NULL,
                              name_expr->lineno, name_expr->col_offset,
                              c->c_arena);
        if (!d) {
            return NULL;
        }
        name_expr = NULL;
    }
    else {
        d = __ast_for_call(c, TINYPY_CST_CHILD(n, 3), name_expr);
        if (!d) {
            return NULL;
        }
        name_expr = NULL;
    }

    return d;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__ast_for_decorators(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t d;
    int32_t i;

    tinypy_ast_sequence_t *decorator_seq = TINYPY_AST_SEQUENCE_NEW(TINYPY_CST_CHILD_COUNT(n), c->c_arena);
    if (!decorator_seq) {
        return NULL;
    }

    for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i++) {
        d = __ast_for_decorator(c, TINYPY_CST_CHILD(n, i));
        if (!d) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(decorator_seq, i, d);
    }
    return decorator_seq;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_funcdef(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, tinypy_ast_sequence_t *decorator_seq) {
    /* TINYPY_GRAMMAR_FUNCDEF: 'def' TINYPY_TOKEN_NAME TINYPY_GRAMMAR_PARAMETERS ':' TINYPY_GRAMMAR_SUITE */
    tinypy_ast_identifier_t name;
    tinypy_ast_arguments_t args;
    int32_t name_i = 1;

    name = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, name_i));
    if (!name) {
        return NULL;
    }
    else if (!__forbidden_check(c, TINYPY_CST_CHILD(n, name_i), TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, name_i)))) {
        return NULL;
    }
    args = __ast_for_arguments(c, TINYPY_CST_CHILD(n, name_i + 1));
    if (!args) {
        return NULL;
    }
    tinypy_ast_sequence_t *body = __ast_for_suite(c, TINYPY_CST_CHILD(n, name_i + 3));
    if (!body) {
        return NULL;
    }

    tinypy_ast_statement_t return_value_1 = __tinypy_ast_function_def(name, args, body, decorator_seq, TINYPY_AST_LINE_NUMBER(n),
                                         n->column_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_decorated(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_DECORATED: TINYPY_GRAMMAR_DECORATORS (TINYPY_GRAMMAR_CLASSDEF | TINYPY_GRAMMAR_FUNCDEF) */
    tinypy_ast_statement_t thing = NULL;
    tinypy_ast_sequence_t *decorator_seq = NULL;

    decorator_seq = __ast_for_decorators(c, TINYPY_CST_CHILD(n, 0));
    if (!decorator_seq) {
        return NULL;
    }

    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 1)) == TINYPY_GRAMMAR_FUNCDEF) {
        thing = __ast_for_funcdef(c, TINYPY_CST_CHILD(n, 1), decorator_seq);
    }
    else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 1)) == TINYPY_GRAMMAR_CLASSDEF) {
        thing = __ast_for_classdef(c, TINYPY_CST_CHILD(n, 1), decorator_seq);
    }
    /* we count the TINYPY_GRAMMAR_DECORATORS in when talking about the class' or
       function's line number */
    if (thing) {
        thing->lineno = TINYPY_AST_LINE_NUMBER(n);
        thing->col_offset = n->column_offset;
    }
    return thing;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_lambdef(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_LAMBDEF: 'lambda' [TINYPY_GRAMMAR_VARARGSLIST] ':' TINYPY_GRAMMAR_TEST */
    tinypy_ast_arguments_t args;
    tinypy_ast_expression_t expression;

    if (TINYPY_CST_CHILD_COUNT(n) == 3) {
        args = __tinypy_ast_arguments(NULL, NULL, NULL, NULL, c->c_arena);
        if (!args) {
            return NULL;
        }
        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 2));
        if (!expression) {
            return NULL;
        }
    }
    else {
        args = __ast_for_arguments(c, TINYPY_CST_CHILD(n, 1));
        if (!args) {
            return NULL;
        }
        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 3));
        if (!expression) {
            return NULL;
        }
    }

    tinypy_ast_expression_t return_value_1 = __tinypy_ast_lambda(args, expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_ifexpr(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_TEST: TINYPY_GRAMMAR_OR_TEST 'if' TINYPY_GRAMMAR_OR_TEST 'else' TINYPY_GRAMMAR_TEST */
    tinypy_ast_expression_t expression, body, orelse;

    body = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
    if (!body) {
        return NULL;
    }
    expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 2));
    if (!expression) {
        return NULL;
    }
    orelse = __ast_for_expr(c, TINYPY_CST_CHILD(n, 4));
    if (!orelse) {
        return NULL;
    }
    tinypy_ast_expression_t return_value_1 = __tinypy_ast_if_exp(expression, body, orelse, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                   c->c_arena);
    return return_value_1;
}

/* XXX(nnorwitz): the listcomp and genexpr code should be refactored
   so there is only a single version.  Possibly for loops can also re-use
   the code.
*/

/* Count the number of 'for' loop in a list comprehension.

   Helper for __ast_for_listcomp().
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __count_list_fors(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    (void)c;
    int32_t n_fors = 0;
    tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 1);

count_list_for:
    n_fors++;
    if (TINYPY_CST_CHILD_COUNT(ch) == 5) {
        ch = TINYPY_CST_CHILD(ch, 4);
    }
    else {
        return n_fors;
    }
count_list_iter:
    ch = TINYPY_CST_CHILD(ch, 0);
    if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_LIST_FOR) {
        goto count_list_for;
    }
    else if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_LIST_IF) {
        if (TINYPY_CST_CHILD_COUNT(ch) == 3) {
            ch = TINYPY_CST_CHILD(ch, 2);
            goto count_list_iter;
        }
        else {
            return n_fors;
        }
    }

    /* Should never be reached */
    TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "logic error in __count_list_fors");
    return -1;
}

/* Count the number of 'if' statements in a list comprehension.

   Helper for __ast_for_listcomp().
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __count_list_ifs(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    (void)c;
    int32_t n_ifs = 0;

count_list_iter:
    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_GRAMMAR_LIST_FOR) {
        return n_ifs;
    }
    n = TINYPY_CST_CHILD(n, 0);
    n_ifs++;
    if (TINYPY_CST_CHILD_COUNT(n) == 2) {
        return n_ifs;
    }
    n = TINYPY_CST_CHILD(n, 2);
    goto count_list_iter;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_listcomp(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_LISTMAKER: TINYPY_GRAMMAR_TEST ( TINYPY_GRAMMAR_LIST_FOR | (',' TINYPY_GRAMMAR_TEST)* [','] )
       TINYPY_GRAMMAR_LIST_FOR: 'for' TINYPY_GRAMMAR_EXPRLIST 'in' TINYPY_GRAMMAR_TESTLIST_SAFE [TINYPY_GRAMMAR_LIST_ITER]
       TINYPY_GRAMMAR_LIST_ITER: TINYPY_GRAMMAR_LIST_FOR | TINYPY_GRAMMAR_LIST_IF
       TINYPY_GRAMMAR_LIST_IF: 'if' TINYPY_GRAMMAR_TEST [TINYPY_GRAMMAR_LIST_ITER]
       TINYPY_GRAMMAR_TESTLIST_SAFE: TINYPY_GRAMMAR_TEST [(',' TINYPY_GRAMMAR_TEST)+ [',']]
    */
    tinypy_ast_expression_t elt, first;
    int32_t i, n_fors;

    elt = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
    if (!elt) {
        return NULL;
    }

    n_fors = __count_list_fors(c, n);
    if (n_fors == -1) {
        return NULL;
    }

    tinypy_ast_sequence_t *listcomps = TINYPY_AST_SEQUENCE_NEW(n_fors, c->c_arena);
    if (!listcomps) {
        return NULL;
    }

    tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 1);
    for (i = 0; i < n_fors; i++) {
        tinypy_ast_comprehension_t lc;
        tinypy_ast_sequence_t *t;
        tinypy_ast_expression_t expression;
        tinypy_cst_node_t *for_ch;

        for_ch = TINYPY_CST_CHILD(ch, 1);
        t = __ast_for_exprlist(c, for_ch, TINYPY_AST_CONTEXT_STORE);
        if (!t) {
            return NULL;
        }
        expression = __ast_for_testlist(c, TINYPY_CST_CHILD(ch, 3));
        if (!expression) {
            return NULL;
        }

        /* Check the # of children rather than the length of t, since
           [x for x, in ... ] has 1 element in t, but still requires a Tuple.
        */
        first = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(t, 0);
        if (TINYPY_CST_CHILD_COUNT(for_ch) == 1) {
            lc = __tinypy_ast_comprehension(first, expression, NULL, c->c_arena);
        }
        else {
            tinypy_ast_expression_t ast_tuple = __tinypy_ast_tuple(t, TINYPY_AST_CONTEXT_STORE, first->lineno, first->col_offset,
                                                                   c->c_arena);
            lc = __tinypy_ast_comprehension(ast_tuple,
                                            expression, NULL, c->c_arena);
        }
        if (!lc) {
            return NULL;
        }

        if (TINYPY_CST_CHILD_COUNT(ch) == 5) {
            int32_t j, n_ifs;
            tinypy_ast_sequence_t *ifs;
            tinypy_ast_expression_t list_for_expr;

            ch = TINYPY_CST_CHILD(ch, 4);
            n_ifs = __count_list_ifs(c, ch);
            if (n_ifs == -1) {
                return NULL;
            }

            ifs = TINYPY_AST_SEQUENCE_NEW(n_ifs, c->c_arena);
            if (!ifs) {
                return NULL;
            }

            for (j = 0; j < n_ifs; j++) {
                ch = TINYPY_CST_CHILD(ch, 0);

                list_for_expr = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 1));
                if (!list_for_expr) {
                    return NULL;
                }

                TINYPY_AST_SEQUENCE_SET(ifs, j, list_for_expr);
                if (TINYPY_CST_CHILD_COUNT(ch) == 3) {
                    ch = TINYPY_CST_CHILD(ch, 2);
                }
            }
            /* on exit, must guarantee that ch is a TINYPY_GRAMMAR_LIST_FOR */
            if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_LIST_ITER) {
                ch = TINYPY_CST_CHILD(ch, 0);
            }
            lc->ifs = ifs;
        }
        TINYPY_AST_SEQUENCE_SET(listcomps, i, lc);
    }

    tinypy_ast_expression_t return_value_1 = __tinypy_ast_list_comp(elt, listcomps, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}

/*
   Count the number of 'for' loops in a comprehension.

   Helper for __ast_for_comprehension().
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __count_comp_fors(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    (void)c;
    int32_t n_fors = 0;

count_comp_for:
    n_fors++;
    if (TINYPY_CST_CHILD_COUNT(n) == 5) {
        n = TINYPY_CST_CHILD(n, 4);
    }
    else {
        return n_fors;
    }
count_comp_iter:
    n = TINYPY_CST_CHILD(n, 0);
    if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_COMP_FOR) {
        goto count_comp_for;
    }
    else if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_COMP_IF) {
        if (TINYPY_CST_CHILD_COUNT(n) == 3) {
            n = TINYPY_CST_CHILD(n, 2);
            goto count_comp_iter;
        }
        else {
            return n_fors;
        }
    }

    /* Should never be reached */
    TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "logic error in __count_comp_fors");
    return -1;
}

/* Count the number of 'if' statements in a comprehension.

   Helper for __ast_for_comprehension().
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __count_comp_ifs(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    (void)c;
    int32_t n_ifs = 0;

    while (1) {
        if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_GRAMMAR_COMP_FOR) {
            return n_ifs;
        }
        n = TINYPY_CST_CHILD(n, 0);
        n_ifs++;
        if (TINYPY_CST_CHILD_COUNT(n) == 2) {
            return n_ifs;
        }
        n = TINYPY_CST_CHILD(n, 2);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__ast_for_comprehension(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    int32_t i, n_fors;

    n_fors = __count_comp_fors(c, n);
    if (n_fors == -1) {
        return NULL;
    }

    tinypy_ast_sequence_t *comps = TINYPY_AST_SEQUENCE_NEW(n_fors, c->c_arena);
    if (!comps) {
        return NULL;
    }

    for (i = 0; i < n_fors; i++) {
        tinypy_ast_comprehension_t comp;
        tinypy_ast_sequence_t *t;
        tinypy_ast_expression_t expression, first;
        tinypy_cst_node_t *for_ch;

        for_ch = TINYPY_CST_CHILD(n, 1);
        t = __ast_for_exprlist(c, for_ch, TINYPY_AST_CONTEXT_STORE);
        if (!t) {
            return NULL;
        }
        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 3));
        if (!expression) {
            return NULL;
        }

        /* Check the # of children rather than the length of t, since
           (x for x, in ...) has 1 element in t, but still requires a Tuple. */
        first = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(t, 0);
        if (TINYPY_CST_CHILD_COUNT(for_ch) == 1) {
            comp = __tinypy_ast_comprehension(first, expression, NULL, c->c_arena);
        }
        else {
            tinypy_ast_expression_t ast_tuple = __tinypy_ast_tuple(t, TINYPY_AST_CONTEXT_STORE, first->lineno, first->col_offset,
                                                                   c->c_arena);
            comp = __tinypy_ast_comprehension(ast_tuple,
                                              expression, NULL, c->c_arena);
        }
        if (!comp) {
            return NULL;
        }

        if (TINYPY_CST_CHILD_COUNT(n) == 5) {
            int32_t j, n_ifs;
            tinypy_ast_sequence_t *ifs;

            n = TINYPY_CST_CHILD(n, 4);
            n_ifs = __count_comp_ifs(c, n);
            if (n_ifs == -1) {
                return NULL;
            }

            ifs = TINYPY_AST_SEQUENCE_NEW(n_ifs, c->c_arena);
            if (!ifs) {
                return NULL;
            }

            for (j = 0; j < n_ifs; j++) {
                n = TINYPY_CST_CHILD(n, 0);

                expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
                if (!expression) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(ifs, j, expression);
                if (TINYPY_CST_CHILD_COUNT(n) == 3) {
                    n = TINYPY_CST_CHILD(n, 2);
                }
            }
            /* on exit, must guarantee that n is a TINYPY_GRAMMAR_COMP_FOR */
            if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_COMP_ITER) {
                n = TINYPY_CST_CHILD(n, 0);
            }
            comp->ifs = ifs;
        }
        TINYPY_AST_SEQUENCE_SET(comps, i, comp);
    }
    return comps;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_itercomp(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, int32_t type) {
    tinypy_ast_expression_t elt;

    elt = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
    if (!elt) {
        return NULL;
    }

    tinypy_ast_sequence_t *comps = __ast_for_comprehension(c, TINYPY_CST_CHILD(n, 1));
    if (!comps) {
        return NULL;
    }

    if (type == TINYPY_AST_BUILDER_COMPREHENSION_GENERATOR) {
        tinypy_ast_expression_t return_value_1 = __tinypy_ast_generator_exp(elt, comps, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_1;
    }
    else if (type == TINYPY_AST_BUILDER_COMPREHENSION_SET) {
        tinypy_ast_expression_t return_value_2 = __tinypy_ast_set_comp(elt, comps, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_2;
    }
    else {
        /* Should never happen */
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_dictcomp(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t key, value;

    key = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
    if (!key) {
        return NULL;
    }

    value = __ast_for_expr(c, TINYPY_CST_CHILD(n, 2));
    if (!value) {
        return NULL;
    }

    tinypy_ast_sequence_t *comps = __ast_for_comprehension(c, TINYPY_CST_CHILD(n, 3));
    if (!comps) {
        return NULL;
    }

    tinypy_ast_expression_t return_value_1 = __tinypy_ast_dict_comp(key, value, comps, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_genexp(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t return_value_1 = __ast_for_itercomp(c, n, TINYPY_AST_BUILDER_COMPREHENSION_GENERATOR);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_setcomp(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t return_value_1 = __ast_for_itercomp(c, n, TINYPY_AST_BUILDER_COMPREHENSION_SET);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_atom(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_ATOM: '(' [TINYPY_GRAMMAR_YIELD_EXPR|TINYPY_GRAMMAR_TESTLIST_COMP] ')' | '[' [TINYPY_GRAMMAR_LISTMAKER] ']'
       | '{' [dictmaker] '}' | '`' TINYPY_GRAMMAR_TESTLIST '`' | TINYPY_TOKEN_NAME | TINYPY_TOKEN_NUMBER | TINYPY_TOKEN_STRING+
    */
    tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 0);

    switch (TINYPY_CST_TYPE(ch)) {
    case TINYPY_TOKEN_NAME: {
        /* All names start in TINYPY_AST_CONTEXT_LOAD context, but may later be
           changed. */
        tinypy_value_t *name = TINYPY_AST_NEW_IDENTIFIER(ch);
        if (!name) {
            return NULL;
        }
        tinypy_ast_expression_t return_value_1 = __tinypy_ast_name(name, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_1;
    }
    case TINYPY_TOKEN_STRING: {
        tinypy_value_t *str = __parsestrplus(c, n);
        if (!str) {
            if (c->c_arena->failed == 0) {
                __ast_error(n, "invalid string literal");
            }
            return NULL;
        }
        TINYPY_COMPILER_ARENA_ADD_VALUE(c->c_arena, str);
        tinypy_ast_expression_t return_value_2 = __tinypy_ast_str(str, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_2;
    }
    case TINYPY_TOKEN_NUMBER: {
        tinypy_value_t *pynum = __parsenumber(c, TINYPY_CST_TEXT(ch));
        if (!pynum) {
            return NULL;
        }

        TINYPY_COMPILER_ARENA_ADD_VALUE(c->c_arena, pynum);
        tinypy_ast_expression_t return_value_3 = __tinypy_ast_num(pynum, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_3;
    }
    case TINYPY_TOKEN_LEFT_PARENTHESIS: /* some parenthesized expressions */
        ch = TINYPY_CST_CHILD(n, 1);

        if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_RIGHT_PARENTHESIS) {
            tinypy_ast_expression_t return_value_4 = __tinypy_ast_tuple(NULL, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_4;
        }

        if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_YIELD_EXPR) {
            tinypy_ast_expression_t return_value_5 = __ast_for_expr(c, ch);
            return return_value_5;
        }

        tinypy_ast_expression_t return_value_6 = __ast_for_testlist_comp(c, ch);
        return return_value_6;
    case TINYPY_TOKEN_LEFT_BRACKET: /* list (or list comprehension) */
        ch = TINYPY_CST_CHILD(n, 1);

        if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_RIGHT_BRACKET) {
            tinypy_ast_expression_t return_value_7 = __tinypy_ast_list(NULL, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_7;
        }

        if (TINYPY_CST_CHILD_COUNT(ch) == 1 || TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 1)) == TINYPY_TOKEN_COMMA) {
            tinypy_ast_sequence_t *elts = __seq_for_testlist(c, ch);
            if (!elts) {
                return NULL;
            }

            tinypy_ast_expression_t return_value_8 = __tinypy_ast_list(elts, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_8;
        }
        else {
            tinypy_ast_expression_t return_value_9 = __ast_for_listcomp(c, ch);
            return return_value_9;
        }
    case TINYPY_TOKEN_LEFT_BRACE: {
        /* TINYPY_GRAMMAR_DICTORSETMAKER:
         *    (TINYPY_GRAMMAR_TEST ':' TINYPY_GRAMMAR_TEST (TINYPY_GRAMMAR_COMP_FOR | (',' TINYPY_GRAMMAR_TEST ':' TINYPY_GRAMMAR_TEST)* [','])) |
         *    (TINYPY_GRAMMAR_TEST (TINYPY_GRAMMAR_COMP_FOR | (',' TINYPY_GRAMMAR_TEST)* [',']))
         */
        int32_t i, size;
        tinypy_ast_sequence_t *keys, *values;

        ch = TINYPY_CST_CHILD(n, 1);
        if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_RIGHT_BRACE) {
            /* it's an empty dict */
            tinypy_ast_expression_t return_value_10 = __tinypy_ast_dict(NULL, NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_10;
        }
        else if (TINYPY_CST_CHILD_COUNT(ch) == 1 || TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 1)) == TINYPY_TOKEN_COMMA) {
            /* it's a simple set */
            tinypy_ast_sequence_t *elts;
            size = (TINYPY_CST_CHILD_COUNT(ch) + 1) / 2; /* +1 in case no trailing comma */
            elts = TINYPY_AST_SEQUENCE_NEW(size, c->c_arena);
            if (!elts) {
                return NULL;
            }
            for (i = 0; i < TINYPY_CST_CHILD_COUNT(ch); i += 2) {
                tinypy_ast_expression_t expression;
                expression = __ast_for_expr(c, TINYPY_CST_CHILD(ch, i));
                if (!expression) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(elts, i / 2, expression);
            }
            tinypy_ast_expression_t return_value_11 = __tinypy_ast_set(elts, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_11;
        }
        else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 1)) == TINYPY_GRAMMAR_COMP_FOR) {
            /* it's a set comprehension */
            tinypy_ast_expression_t return_value_12 = __ast_for_setcomp(c, ch);
            return return_value_12;
        }
        else if (TINYPY_CST_CHILD_COUNT(ch) > 3 && TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 3)) == TINYPY_GRAMMAR_COMP_FOR) {
            tinypy_ast_expression_t return_value_13 = __ast_for_dictcomp(c, ch);
            return return_value_13;
        }
        else {
            /* it's a dict */
            size = (TINYPY_CST_CHILD_COUNT(ch) + 1) / 4; /* +1 in case no trailing comma */
            keys = TINYPY_AST_SEQUENCE_NEW(size, c->c_arena);
            if (!keys) {
                return NULL;
            }

            values = TINYPY_AST_SEQUENCE_NEW(size, c->c_arena);
            if (!values) {
                return NULL;
            }

            for (i = 0; i < TINYPY_CST_CHILD_COUNT(ch); i += 4) {
                tinypy_ast_expression_t expression;

                expression = __ast_for_expr(c, TINYPY_CST_CHILD(ch, i));
                if (!expression) {
                    return NULL;
                }

                TINYPY_AST_SEQUENCE_SET(keys, i / 4, expression);

                expression = __ast_for_expr(c, TINYPY_CST_CHILD(ch, i + 2));
                if (!expression) {
                    return NULL;
                }

                TINYPY_AST_SEQUENCE_SET(values, i / 4, expression);
            }
            tinypy_ast_expression_t return_value_14 = __tinypy_ast_dict(keys, values, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_14;
        }
    }
    case TINYPY_TOKEN_BACKQUOTE: { /* repr */
        tinypy_ast_expression_t expression;
        expression = __ast_for_testlist(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }

        tinypy_ast_expression_t return_value_15 = __tinypy_ast_repr(expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_15;
    }
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "unhandled TINYPY_GRAMMAR_ATOM %d", TINYPY_CST_TYPE(ch));
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_slice_t __ast_for_slice(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t lower = NULL, upper = NULL, step = NULL;

    /* TINYPY_GRAMMAR_SUBSCRIPT: '.' '.' '.' | TINYPY_GRAMMAR_TEST | [TINYPY_GRAMMAR_TEST] ':' [TINYPY_GRAMMAR_TEST] [TINYPY_GRAMMAR_SLICEOP]
       TINYPY_GRAMMAR_SLICEOP: ':' [TINYPY_GRAMMAR_TEST]
    */
    tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 0);
    if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_DOT) {
        tinypy_ast_slice_t return_value_1 = __tinypy_ast_ellipsis(c->c_arena);
        return return_value_1;
    }

    if (TINYPY_CST_CHILD_COUNT(n) == 1 && TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_TEST) {
        /* 'step' variable hold no significance in terms of being used over
           other vars */
        step = __ast_for_expr(c, ch);
        if (!step) {
            return NULL;
        }

        tinypy_ast_slice_t return_value_2 = __tinypy_ast_index(step, c->c_arena);
        return return_value_2;
    }

    if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_TEST) {
        lower = __ast_for_expr(c, ch);
        if (!lower) {
            return NULL;
        }
    }

    /* If there's an upper bound it's in the second or third position. */
    if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_COLON) {
        if (TINYPY_CST_CHILD_COUNT(n) > 1) {
            tinypy_cst_node_t *n2 = TINYPY_CST_CHILD(n, 1);

            if (TINYPY_CST_TYPE(n2) == TINYPY_GRAMMAR_TEST) {
                upper = __ast_for_expr(c, n2);
                if (!upper) {
                    return NULL;
                }
            }
        }
    }
    else if (TINYPY_CST_CHILD_COUNT(n) > 2) {
        tinypy_cst_node_t *n2 = TINYPY_CST_CHILD(n, 2);

        if (TINYPY_CST_TYPE(n2) == TINYPY_GRAMMAR_TEST) {
            upper = __ast_for_expr(c, n2);
            if (!upper) {
                return NULL;
            }
        }
    }

    ch = TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1);
    if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_SLICEOP) {
        if (TINYPY_CST_CHILD_COUNT(ch) == 1) {
            /* This is an extended slice (ie "x[::]") with no expression in the
              step field. We set this literally to "None" in order to
              disambiguate it from x[:]. (The interpreter might have to call
              __getslice__ for x[:], but it must call __getitem__ for x[::].)
            */
            tinypy_ast_identifier_t none = __new_identifier("None", c->c_arena);
            if (!none) {
                return NULL;
            }
            ch = TINYPY_CST_CHILD(ch, 0);
            step = __tinypy_ast_name(none, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(ch), ch->column_offset, c->c_arena);
            if (!step) {
                return NULL;
            }
        }
        else {
            ch = TINYPY_CST_CHILD(ch, 1);
            if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_TEST) {
                step = __ast_for_expr(c, ch);
                if (!step) {
                    return NULL;
                }
            }
        }
    }

    tinypy_ast_slice_t return_value_3 = __tinypy_ast_slice(lower, upper, step, c->c_arena);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_binop(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* Must account for a sequence of expressions.
       How should A op B op C by represented?
       __tinypy_ast_bin_op(__tinypy_ast_bin_op(A, op, B), op, C).
    */

    int32_t i, nops;
    tinypy_ast_expression_t expr1, expr2, result;
    tinypy_ast_binary_operator_e newoperator;

    expr1 = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
    if (!expr1) {
        return NULL;
    }

    expr2 = __ast_for_expr(c, TINYPY_CST_CHILD(n, 2));
    if (!expr2) {
        return NULL;
    }

    newoperator = __get_operator(TINYPY_CST_CHILD(n, 1));
    if (!newoperator) {
        return NULL;
    }

    result = __tinypy_ast_bin_op(expr1, newoperator, expr2, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                 c->c_arena);
    if (!result) {
        return NULL;
    }

    nops = (TINYPY_CST_CHILD_COUNT(n) - 1) / 2;
    for (i = 1; i < nops; i++) {
        tinypy_ast_expression_t tmp_result, tmp;
        const tinypy_cst_node_t *next_oper = TINYPY_CST_CHILD(n, i * 2 + 1);

        newoperator = __get_operator(next_oper);
        if (!newoperator) {
            return NULL;
        }

        tmp = __ast_for_expr(c, TINYPY_CST_CHILD(n, i * 2 + 2));
        if (!tmp) {
            return NULL;
        }

        tmp_result = __tinypy_ast_bin_op(result, newoperator, tmp,
                                         TINYPY_AST_LINE_NUMBER(next_oper), next_oper->column_offset,
                                         c->c_arena);
        if (!tmp_result) {
            return NULL;
        }
        result = tmp_result;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_trailer(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, tinypy_ast_expression_t left_expr) {
    /* TINYPY_GRAMMAR_TRAILER: '(' [TINYPY_GRAMMAR_ARGLIST] ')' | '[' TINYPY_GRAMMAR_SUBSCRIPTLIST ']' | '.' TINYPY_TOKEN_NAME
       TINYPY_GRAMMAR_SUBSCRIPTLIST: TINYPY_GRAMMAR_SUBSCRIPT (',' TINYPY_GRAMMAR_SUBSCRIPT)* [',']
       TINYPY_GRAMMAR_SUBSCRIPT: '.' '.' '.' | TINYPY_GRAMMAR_TEST | [TINYPY_GRAMMAR_TEST] ':' [TINYPY_GRAMMAR_TEST] [TINYPY_GRAMMAR_SLICEOP]
     */
    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_TOKEN_LEFT_PARENTHESIS) {
        if (TINYPY_CST_CHILD_COUNT(n) == 2) {
            tinypy_ast_expression_t return_value_2 = __tinypy_ast_call(left_expr, NULL, NULL, NULL, NULL, TINYPY_AST_LINE_NUMBER(n),
                                                 n->column_offset, c->c_arena);
            return return_value_2;
        }
        else {
            tinypy_ast_expression_t return_value_1 = __ast_for_call(c, TINYPY_CST_CHILD(n, 1), left_expr);
            return return_value_1;
        }
    }
    else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_TOKEN_DOT) {
        tinypy_value_t *attr_id = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, 1));
        if (!attr_id) {
            return NULL;
        }
        tinypy_ast_expression_t return_value_3 = __tinypy_ast_attribute(left_expr, attr_id, TINYPY_AST_CONTEXT_LOAD,
                                              TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_3;
    }
    else {
        n = TINYPY_CST_CHILD(n, 1);
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            tinypy_ast_slice_t slc = __ast_for_slice(c, TINYPY_CST_CHILD(n, 0));
            if (!slc) {
                return NULL;
            }
            tinypy_ast_expression_t return_value_4 = __tinypy_ast_subscript(left_expr, slc, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                      c->c_arena);
            return return_value_4;
        }
        else {
            /* The grammar is ambiguous here. The ambiguity is resolved
               by treating the sequence as a tuple literal if there are
               no slice features.
            */
            int32_t j;
            tinypy_ast_slice_t slc;
            tinypy_ast_expression_t e;
            tinypy_compiler_boolean_e simple = TINYPY_COMPILER_TRUE;
            tinypy_ast_sequence_t *slices, *elts;
            slices = TINYPY_AST_SEQUENCE_NEW((TINYPY_CST_CHILD_COUNT(n) + 1) / 2, c->c_arena);
            if (!slices) {
                return NULL;
            }
            for (j = 0; j < TINYPY_CST_CHILD_COUNT(n); j += 2) {
                slc = __ast_for_slice(c, TINYPY_CST_CHILD(n, j));
                if (!slc) {
                    return NULL;
                }
                if (slc->kind != TINYPY_AST_KIND_INDEX) {
                    simple = TINYPY_COMPILER_FALSE;
                }
                TINYPY_AST_SEQUENCE_SET(slices, j / 2, slc);
            }
            if (!simple) {
                tinypy_ast_slice_t ast_ext_slice = __tinypy_ast_ext_slice(slices, c->c_arena);
                tinypy_ast_expression_t return_value_5 = __tinypy_ast_subscript(left_expr, ast_ext_slice,
                                                              TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
                return return_value_5;
            }
            /* extract Index values and put them in a Tuple */
            elts = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(slices), c->c_arena);
            if (!elts) {
                return NULL;
            }
            for (j = 0; j < TINYPY_AST_SEQUENCE_LENGTH(slices); ++j) {
                slc = (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(slices, j);
                TINYPY_AST_SEQUENCE_SET(elts, j, slc->v.Index.value);
            }
            e = __tinypy_ast_tuple(elts, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            if (!e) {
                return NULL;
            }
            tinypy_ast_slice_t ast_index = __tinypy_ast_index(e, c->c_arena);
            tinypy_ast_expression_t return_value_6 = __tinypy_ast_subscript(left_expr, ast_index,
                                                      TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_6;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_factor(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t function_result;
    tinypy_cst_node_t *pfactor, *ppower, *patom, *pnum;
    tinypy_ast_expression_t expression;

    /* If the unary - operator is applied to a constant, don't generate
       a TINYPY_OP_UNARY_NEGATIVE opcode.  Just store the approriate value as a
       constant.  The peephole optimizer already does something like
       this but it doesn't handle the case where the constant is
       (sys.maxint - 1). In that case, we want a tinypy integer, not a
       tinypy_compiler_long_t.
    */
    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_TOKEN_MINUS && TINYPY_CST_CHILD_COUNT(n) == 2 && TINYPY_CST_TYPE((pfactor = TINYPY_CST_CHILD(n, 1))) == TINYPY_GRAMMAR_FACTOR && TINYPY_CST_CHILD_COUNT(pfactor) == 1 && TINYPY_CST_TYPE((ppower = TINYPY_CST_CHILD(pfactor, 0))) == TINYPY_GRAMMAR_POWER && TINYPY_CST_CHILD_COUNT(ppower) == 1 && TINYPY_CST_TYPE((patom = TINYPY_CST_CHILD(ppower, 0))) == TINYPY_GRAMMAR_ATOM && TINYPY_CST_TYPE((pnum = TINYPY_CST_CHILD(patom, 0))) == TINYPY_TOKEN_NUMBER) {
        tinypy_value_t *pynum;
        char *s = (char *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena, strlen(TINYPY_CST_TEXT(pnum)) + 2);
        if (s == NULL) {
            return NULL;
        }
        s[0] = '-';
        strcpy(s + 1, TINYPY_CST_TEXT(pnum));
        pynum = __parsenumber(c, s);
        if (!pynum) {
            return NULL;
        }

        TINYPY_COMPILER_ARENA_ADD_VALUE(c->c_arena, pynum);
        tinypy_ast_expression_t return_value_1 = __tinypy_ast_num(pynum, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_1;
    }

    expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
    if (!expression) {
        return NULL;
    }

    switch (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0))) {
    case TINYPY_TOKEN_PLUS:
        function_result = __tinypy_ast_unary_op(TINYPY_AST_UNARY_ADD, expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                             c->c_arena);
        return function_result;
    case TINYPY_TOKEN_MINUS:
        function_result = __tinypy_ast_unary_op(TINYPY_AST_UNARY_SUBTRACT, expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                             c->c_arena);
        return function_result;
    case TINYPY_TOKEN_TILDE:
        function_result = __tinypy_ast_unary_op(TINYPY_AST_UNARY_INVERT, expression, TINYPY_AST_LINE_NUMBER(n),
                                             n->column_offset, c->c_arena);
        return function_result;
    }
    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "unhandled TINYPY_GRAMMAR_FACTOR: %d",
                               TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)));
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_power(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_POWER: TINYPY_GRAMMAR_ATOM TINYPY_GRAMMAR_TRAILER* ('**' TINYPY_GRAMMAR_FACTOR)*  */
    int32_t i;
    tinypy_ast_expression_t e, tmp;
    e = __ast_for_atom(c, TINYPY_CST_CHILD(n, 0));
    if (!e) {
        return NULL;
    }
    if (TINYPY_CST_CHILD_COUNT(n) == 1) {
        return e;
    }
    for (i = 1; i < TINYPY_CST_CHILD_COUNT(n); i++) {
        tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, i);
        if (TINYPY_CST_TYPE(ch) != TINYPY_GRAMMAR_TRAILER) {
            break;
        }
        tmp = __ast_for_trailer(c, ch, e);
        if (!tmp) {
            return NULL;
        }
        tmp->lineno = e->lineno;
        tmp->col_offset = e->col_offset;
        e = tmp;
    }
    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1)) == TINYPY_GRAMMAR_FACTOR) {
        tinypy_ast_expression_t f = __ast_for_expr(c, TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1));
        if (!f) {
            return NULL;
        }
        tmp = __tinypy_ast_bin_op(e, TINYPY_AST_BINARY_POWER, f, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        if (!tmp) {
            return NULL;
        }
        e = tmp;
    }
    return e;
}

/* Do not name a variable 'TINYPY_GRAMMAR_EXPR'!  Will cause a compile error.
 */

//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_expr(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t function_result;
    /* handle the full range of simple expressions
       TINYPY_GRAMMAR_TEST: TINYPY_GRAMMAR_OR_TEST ['if' TINYPY_GRAMMAR_OR_TEST 'else' TINYPY_GRAMMAR_TEST] | TINYPY_GRAMMAR_LAMBDEF
       TINYPY_GRAMMAR_OR_TEST: TINYPY_GRAMMAR_AND_TEST ('or' TINYPY_GRAMMAR_AND_TEST)*
       TINYPY_GRAMMAR_AND_TEST: TINYPY_GRAMMAR_NOT_TEST ('and' TINYPY_GRAMMAR_NOT_TEST)*
       TINYPY_GRAMMAR_NOT_TEST: 'not' TINYPY_GRAMMAR_NOT_TEST | TINYPY_GRAMMAR_COMPARISON
       TINYPY_GRAMMAR_COMPARISON: TINYPY_GRAMMAR_EXPR (TINYPY_GRAMMAR_COMP_OP TINYPY_GRAMMAR_EXPR)*
       TINYPY_GRAMMAR_EXPR: TINYPY_GRAMMAR_XOR_EXPR ('|' TINYPY_GRAMMAR_XOR_EXPR)*
       TINYPY_GRAMMAR_XOR_EXPR: TINYPY_GRAMMAR_AND_EXPR ('^' TINYPY_GRAMMAR_AND_EXPR)*
       TINYPY_GRAMMAR_AND_EXPR: TINYPY_GRAMMAR_SHIFT_EXPR ('&' TINYPY_GRAMMAR_SHIFT_EXPR)*
       TINYPY_GRAMMAR_SHIFT_EXPR: TINYPY_GRAMMAR_ARITH_EXPR (('<<'|'>>') TINYPY_GRAMMAR_ARITH_EXPR)*
       TINYPY_GRAMMAR_ARITH_EXPR: TINYPY_GRAMMAR_TERM (('+'|'-') TINYPY_GRAMMAR_TERM)*
       TINYPY_GRAMMAR_TERM: TINYPY_GRAMMAR_FACTOR (('*'|'/'|'%'|'//') TINYPY_GRAMMAR_FACTOR)*
       TINYPY_GRAMMAR_FACTOR: ('+'|'-'|'~') TINYPY_GRAMMAR_FACTOR | TINYPY_GRAMMAR_POWER
       TINYPY_GRAMMAR_POWER: TINYPY_GRAMMAR_ATOM TINYPY_GRAMMAR_TRAILER* ('**' TINYPY_GRAMMAR_FACTOR)*

       As well as modified versions that exist for backward compatibility,
       to explicitly allow:
       [ x for x in lambda: 0, lambda: 1 ]
       (which would be ambiguous without these extra rules)

       TINYPY_GRAMMAR_OLD_TEST: TINYPY_GRAMMAR_OR_TEST | TINYPY_GRAMMAR_OLD_LAMBDEF
       TINYPY_GRAMMAR_OLD_LAMBDEF: 'lambda' [vararglist] ':' TINYPY_GRAMMAR_OLD_TEST

    */

    tinypy_ast_sequence_t *seq;
    int32_t i;

loop:
    switch (TINYPY_CST_TYPE(n)) {
    case TINYPY_GRAMMAR_TEST:
    case TINYPY_GRAMMAR_OLD_TEST:
        if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_GRAMMAR_LAMBDEF || TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_GRAMMAR_OLD_LAMBDEF) {
            tinypy_ast_expression_t return_value_1 = __ast_for_lambdef(c, TINYPY_CST_CHILD(n, 0));
            return return_value_1;
        }
        else if (TINYPY_CST_CHILD_COUNT(n) > 1) {
            tinypy_ast_expression_t return_value_2 = __ast_for_ifexpr(c, n);
            return return_value_2;
        }
        /* Fallthrough */
    case TINYPY_GRAMMAR_OR_TEST:
    case TINYPY_GRAMMAR_AND_TEST:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            n = TINYPY_CST_CHILD(n, 0);
            goto loop;
        }
        seq = TINYPY_AST_SEQUENCE_NEW((TINYPY_CST_CHILD_COUNT(n) + 1) / 2, c->c_arena);
        if (!seq) {
            return NULL;
        }
        for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
            tinypy_ast_expression_t e = __ast_for_expr(c, TINYPY_CST_CHILD(n, i));
            if (!e) {
                return NULL;
            }
            TINYPY_AST_SEQUENCE_SET(seq, i / 2, e);
        }
        if (!strcmp(TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 1)), "and")) {
            tinypy_ast_expression_t return_value_7 = __tinypy_ast_bool_op(TINYPY_AST_BOOLEAN_AND, seq, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                    c->c_arena);
            return return_value_7;
        }
        tinypy_ast_expression_t return_value_3 = __tinypy_ast_bool_op(TINYPY_AST_BOOLEAN_OR, seq, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_3;
    case TINYPY_GRAMMAR_NOT_TEST:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            n = TINYPY_CST_CHILD(n, 0);
            goto loop;
        }
        else {
            tinypy_ast_expression_t expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
            if (!expression) {
                return NULL;
            }

            tinypy_ast_expression_t return_value_8 = __tinypy_ast_unary_op(TINYPY_AST_UNARY_NOT, expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                     c->c_arena);
            return return_value_8;
        }
    case TINYPY_GRAMMAR_COMPARISON:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            n = TINYPY_CST_CHILD(n, 0);
            goto loop;
        }
        else {
            tinypy_ast_expression_t expression;
            tinypy_ast_integer_sequence_t *ops;
            tinypy_ast_sequence_t *cmps;
            ops = TINYPY_AST_INTEGER_SEQUENCE_NEW(TINYPY_CST_CHILD_COUNT(n) / 2, c->c_arena);
            if (!ops) {
                return NULL;
            }
            cmps = TINYPY_AST_SEQUENCE_NEW(TINYPY_CST_CHILD_COUNT(n) / 2, c->c_arena);
            if (!cmps) {
                return NULL;
            }
            for (i = 1; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
                tinypy_ast_compare_operator_e newoperator;

                newoperator = __ast_for_comp_op(c, TINYPY_CST_CHILD(n, i));
                if (!newoperator) {
                    return NULL;
                }

                expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, i + 1));
                if (!expression) {
                    return NULL;
                }

                TINYPY_AST_SEQUENCE_SET(ops, i / 2, newoperator);
                TINYPY_AST_SEQUENCE_SET(cmps, i / 2, expression);
            }
            expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
            if (!expression) {
                return NULL;
            }

            tinypy_ast_expression_t return_value_9 = __tinypy_ast_compare(expression, ops, cmps, TINYPY_AST_LINE_NUMBER(n),
                                                    n->column_offset, c->c_arena);
            return return_value_9;
        }
        break;

    /* The next five cases all handle BinOps.  The main body of code
       is the same in each case, but the switch turned inside out to
       reuse the code for each type of operator.
     */
    case TINYPY_GRAMMAR_EXPR:
    case TINYPY_GRAMMAR_XOR_EXPR:
    case TINYPY_GRAMMAR_AND_EXPR:
    case TINYPY_GRAMMAR_SHIFT_EXPR:
    case TINYPY_GRAMMAR_ARITH_EXPR:
    case TINYPY_GRAMMAR_TERM:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            n = TINYPY_CST_CHILD(n, 0);
            goto loop;
        }
        tinypy_ast_expression_t return_value_4 = __ast_for_binop(c, n);
        return return_value_4;
    case TINYPY_GRAMMAR_YIELD_EXPR: {
        tinypy_ast_expression_t exp = NULL;
        if (TINYPY_CST_CHILD_COUNT(n) == 2) {
            exp = __ast_for_testlist(c, TINYPY_CST_CHILD(n, 1));
            if (!exp) {
                return NULL;
            }
        }
        tinypy_ast_expression_t return_value_5 = __tinypy_ast_yield(exp, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_5;
    }
    case TINYPY_GRAMMAR_FACTOR:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            n = TINYPY_CST_CHILD(n, 0);
            goto loop;
        }
        tinypy_ast_expression_t return_value_6 = __ast_for_factor(c, n);
        return return_value_6;
    case TINYPY_GRAMMAR_POWER:
        function_result = __ast_for_power(c, n);
        return function_result;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "unhandled TINYPY_GRAMMAR_EXPR: %d", TINYPY_CST_TYPE(n));
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_call(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, tinypy_ast_expression_t func) {
    /* TINYPY_GRAMMAR_ARGLIST: (TINYPY_GRAMMAR_ARGUMENT ',')* (TINYPY_GRAMMAR_ARGUMENT [',']| '*' TINYPY_GRAMMAR_TEST [',' '**' TINYPY_GRAMMAR_TEST]
               | '**' TINYPY_GRAMMAR_TEST)
      TINYPY_GRAMMAR_ARGUMENT: [TINYPY_GRAMMAR_TEST '='] TINYPY_GRAMMAR_TEST [TINYPY_GRAMMAR_COMP_FOR]        # Really [keyword '='] TINYPY_GRAMMAR_TEST
    */

    int32_t i, nargs, nkeywords, ngens;
    tinypy_ast_expression_t vararg = NULL, kwarg = NULL;

    nargs = 0;
    nkeywords = 0;
    ngens = 0;
    for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i++) {
        tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, i);
        if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_ARGUMENT) {
            if (TINYPY_CST_CHILD_COUNT(ch) == 1) {
                nargs++;
            }
            else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 1)) == TINYPY_GRAMMAR_COMP_FOR) {
                ngens++;
            }
            else {
                nkeywords++;
            }
        }
    }
    if (ngens > 1 || (ngens && (nargs || nkeywords))) {
        __ast_error(n, "Generator expression must be parenthesized "
                       "if not sole TINYPY_GRAMMAR_ARGUMENT");
        return NULL;
    }

    if (nargs + nkeywords + ngens > 255) {
        __ast_error(n, "more than 255 arguments");
        return NULL;
    }

    tinypy_ast_sequence_t *args = TINYPY_AST_SEQUENCE_NEW(nargs + ngens, c->c_arena);
    if (!args) {
        return NULL;
    }
    tinypy_ast_sequence_t *keywords = TINYPY_AST_SEQUENCE_NEW(nkeywords, c->c_arena);
    if (!keywords) {
        return NULL;
    }
    nargs = 0;
    nkeywords = 0;
    for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i++) {
        tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, i);
        if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_ARGUMENT) {
            tinypy_ast_expression_t e;
            if (TINYPY_CST_CHILD_COUNT(ch) == 1) {
                if (nkeywords) {
                    __ast_error(TINYPY_CST_CHILD(ch, 0),
                                "non-keyword arg after keyword arg");
                    return NULL;
                }
                if (vararg) {
                    __ast_error(TINYPY_CST_CHILD(ch, 0),
                                "only named arguments may follow *expression");
                    return NULL;
                }
                e = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 0));
                if (!e) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(args, nargs++, e);
            }
            else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(ch, 1)) == TINYPY_GRAMMAR_COMP_FOR) {
                e = __ast_for_genexp(c, ch);
                if (!e) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(args, nargs++, e);
            }
            else {
                tinypy_ast_keyword_t kw;
                tinypy_ast_identifier_t key;
                int32_t k;
                char *tmp;

                /* TINYPY_CST_CHILD(ch, 0) is TINYPY_GRAMMAR_TEST, but must be an tinypy_ast_identifier_t? */
                e = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 0));
                if (!e) {
                    return NULL;
                }
                /* f(lambda x: x[0] = 3) ends up getting parsed with
                 * LHS TINYPY_GRAMMAR_TEST = lambda x: x[0], and RHS TINYPY_GRAMMAR_TEST = 3.
                 * SF bug 132313 points out that complaining about a keyword
                 * then is very confusing.
                 */
                if (e->kind == TINYPY_AST_KIND_LAMBDA) {
                    __ast_error(TINYPY_CST_CHILD(ch, 0),
                                "lambda cannot contain assignment");
                    return NULL;
                }
                else if (e->kind != TINYPY_AST_KIND_NAME) {
                    __ast_error(TINYPY_CST_CHILD(ch, 0), "keyword can't be an expression");
                    return NULL;
                }
                key = e->v.Name.id;
                if (!__forbidden_check(c, TINYPY_CST_CHILD(ch, 0), TINYPY_COMPILER_BYTES_AS_STRING(key))) {
                    return NULL;
                }
                for (k = 0; k < nkeywords; k++) {
                    tmp = TINYPY_COMPILER_STRING_AS_STRING(
                        ((tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(keywords, k))->arg);
                    if (!strcmp(tmp, TINYPY_COMPILER_STRING_AS_STRING(key))) {
                        __ast_error(TINYPY_CST_CHILD(ch, 0), "keyword TINYPY_GRAMMAR_ARGUMENT repeated");
                        return NULL;
                    }
                }
                e = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 2));
                if (!e) {
                    return NULL;
                }
                kw = __tinypy_ast_keyword(key, e, c->c_arena);
                if (!kw) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(keywords, nkeywords++, kw);
            }
        }
        else if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_STAR) {
            vararg = __ast_for_expr(c, TINYPY_CST_CHILD(n, i + 1));
            if (!vararg) {
                return NULL;
            }
            i++;
        }
        else if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_DOUBLE_STAR) {
            kwarg = __ast_for_expr(c, TINYPY_CST_CHILD(n, i + 1));
            if (!kwarg) {
                return NULL;
            }
            i++;
        }
    }

    tinypy_ast_expression_t return_value_1 = __tinypy_ast_call(func, args, keywords, vararg, kwarg, func->lineno,
                                 func->col_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_testlist(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_TESTLIST_COMP: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* [','] */
    /* TINYPY_GRAMMAR_TESTLIST: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* [','] */
    /* TINYPY_GRAMMAR_TESTLIST_SAFE: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)+ [','] */
    /* TINYPY_GRAMMAR_TESTLIST1: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* */
    if (TINYPY_CST_CHILD_COUNT(n) == 1) {
        tinypy_ast_expression_t return_value_1 = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
        return return_value_1;
    }
    else {
        tinypy_ast_sequence_t *tmp = __seq_for_testlist(c, n);
        if (!tmp) {
            return NULL;
        }
        tinypy_ast_expression_t return_value_2 = __tinypy_ast_tuple(tmp, TINYPY_AST_CONTEXT_LOAD, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_2;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __ast_for_testlist_comp(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_TESTLIST_COMP: TINYPY_GRAMMAR_TEST ( TINYPY_GRAMMAR_COMP_FOR | (',' TINYPY_GRAMMAR_TEST)* [','] ) */
    /* TINYPY_GRAMMAR_ARGUMENT: TINYPY_GRAMMAR_TEST [ TINYPY_GRAMMAR_COMP_FOR ] */
    if (TINYPY_CST_CHILD_COUNT(n) > 1 && TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 1)) == TINYPY_GRAMMAR_COMP_FOR) {
        tinypy_ast_expression_t return_value_1 = __ast_for_genexp(c, n);
        return return_value_1;
    }
    tinypy_ast_expression_t return_value_2 = __ast_for_testlist(c, n);
    return return_value_2;
}

/* like __ast_for_testlist() but returns a sequence */
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__ast_for_class_bases(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_TESTLIST: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* [','] */
    if (TINYPY_CST_CHILD_COUNT(n) == 1) {
        tinypy_ast_expression_t base;
        tinypy_ast_sequence_t *bases = TINYPY_AST_SEQUENCE_NEW(1, c->c_arena);
        if (!bases) {
            return NULL;
        }
        base = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
        if (!base) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(bases, 0, base);
        return bases;
    }

    tinypy_ast_sequence_t *return_value_1 = __seq_for_testlist(c, n);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_expr_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_EXPR_STMT: TINYPY_GRAMMAR_TESTLIST (TINYPY_GRAMMAR_AUGASSIGN (TINYPY_GRAMMAR_YIELD_EXPR|TINYPY_GRAMMAR_TESTLIST)
                | ('=' (TINYPY_GRAMMAR_YIELD_EXPR|TINYPY_GRAMMAR_TESTLIST))*)
       TINYPY_GRAMMAR_TESTLIST: TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* [',']
       TINYPY_GRAMMAR_AUGASSIGN: '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^='
                | '<<=' | '>>=' | '**=' | '//='
       TINYPY_GRAMMAR_TEST: ... here starts the operator precedence dance
     */

    if (TINYPY_CST_CHILD_COUNT(n) == 1) {
        tinypy_ast_expression_t e = __ast_for_testlist(c, TINYPY_CST_CHILD(n, 0));
        if (!e) {
            return NULL;
        }

        tinypy_ast_statement_t return_value_1 = __tinypy_ast_expr(e, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_1;
    }
    else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 1)) == TINYPY_GRAMMAR_AUGASSIGN) {
        tinypy_ast_expression_t expr1, expr2;
        tinypy_ast_binary_operator_e newoperator;
        tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 0);

        expr1 = __ast_for_testlist(c, ch);
        if (!expr1) {
            return NULL;
        }
        if (!__set_context(c, expr1, TINYPY_AST_CONTEXT_STORE, ch)) {
            return NULL;
        }
        /* __set_context checks that most expressions are not the left side.
          Augmented assignments can only have a name, a TINYPY_GRAMMAR_SUBSCRIPT, or an
          attribute on the left, though, so we have to explicitly check for
          those. */
        switch (expr1->kind) {
        case TINYPY_AST_KIND_NAME:
        case TINYPY_AST_KIND_ATTRIBUTE:
        case TINYPY_AST_KIND_SUBSCRIPT:
            break;
        default:
            __ast_error(ch, "illegal expression for augmented assignment");
            return NULL;
        }

        ch = TINYPY_CST_CHILD(n, 2);
        if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_TESTLIST) {
            expr2 = __ast_for_testlist(c, ch);
        }
        else {
            expr2 = __ast_for_expr(c, ch);
        }
        if (!expr2) {
            return NULL;
        }

        newoperator = __ast_for_augassign(c, TINYPY_CST_CHILD(n, 1));
        if (!newoperator) {
            return NULL;
        }

        tinypy_ast_statement_t return_value_2 = __tinypy_ast_aug_assign(expr1, newoperator, expr2, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                               c->c_arena);
        return return_value_2;
    }
    else {
        int32_t i;
        tinypy_ast_sequence_t *targets;
        tinypy_cst_node_t *value;
        tinypy_ast_expression_t expression;

        /* a normal assignment */
        targets = TINYPY_AST_SEQUENCE_NEW(TINYPY_CST_CHILD_COUNT(n) / 2, c->c_arena);
        if (!targets) {
            return NULL;
        }
        for (i = 0; i < TINYPY_CST_CHILD_COUNT(n) - 2; i += 2) {
            tinypy_ast_expression_t e;
            tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, i);
            if (TINYPY_CST_TYPE(ch) == TINYPY_GRAMMAR_YIELD_EXPR) {
                __ast_error(ch, "assignment to yield expression not possible");
                return NULL;
            }
            e = __ast_for_testlist(c, ch);
            if (!e) {
                return NULL;
            }

            /* set context to assign */
            if (!__set_context(c, e, TINYPY_AST_CONTEXT_STORE, TINYPY_CST_CHILD(n, i))) {
                return NULL;
            }

            TINYPY_AST_SEQUENCE_SET(targets, i / 2, e);
        }
        value = TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1);
        if (TINYPY_CST_TYPE(value) == TINYPY_GRAMMAR_TESTLIST) {
            expression = __ast_for_testlist(c, value);
        }
        else {
            expression = __ast_for_expr(c, value);
        }
        if (!expression) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_3 = __tinypy_ast_assign(targets, expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                           c->c_arena);
        return return_value_3;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_print_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_PRINT_STMT: 'print' ( [ TINYPY_GRAMMAR_TEST (',' TINYPY_GRAMMAR_TEST)* [','] ]
                             | '>>' TINYPY_GRAMMAR_TEST [ (',' TINYPY_GRAMMAR_TEST)+ [','] ] )
     */
    tinypy_ast_expression_t dest = NULL, expression;
    tinypy_ast_sequence_t *seq = NULL;
    tinypy_compiler_boolean_e nl;
    int32_t i, j, values_count, start = 1;

    if (TINYPY_CST_CHILD_COUNT(n) >= 2 && TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 1)) == TINYPY_TOKEN_RIGHT_SHIFT) {
        dest = __ast_for_expr(c, TINYPY_CST_CHILD(n, 2));
        if (!dest) {
            return NULL;
        }
        start = 4;
    }
    values_count = (TINYPY_CST_CHILD_COUNT(n) + 1 - start) / 2;
    if (values_count) {
        seq = TINYPY_AST_SEQUENCE_NEW(values_count, c->c_arena);
        if (!seq) {
            return NULL;
        }
        for (i = start, j = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2, ++j) {
            expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, i));
            if (!expression) {
                return NULL;
            }
            TINYPY_AST_SEQUENCE_SET(seq, j, expression);
        }
    }
    nl = (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1)) == TINYPY_TOKEN_COMMA) ? TINYPY_COMPILER_FALSE : TINYPY_COMPILER_TRUE;
    tinypy_ast_statement_t return_value_1 = __tinypy_ast_print(dest, seq, nl, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__ast_for_exprlist(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, tinypy_ast_expression_context_e context) {
    int32_t i;
    tinypy_ast_expression_t e;

    tinypy_ast_sequence_t *seq = TINYPY_AST_SEQUENCE_NEW((TINYPY_CST_CHILD_COUNT(n) + 1) / 2, c->c_arena);
    if (!seq) {
        return NULL;
    }
    for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
        e = __ast_for_expr(c, TINYPY_CST_CHILD(n, i));
        if (!e) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(seq, i / 2, e);
        if (context && !__set_context(c, e, context, TINYPY_CST_CHILD(n, i))) {
            return NULL;
        }
    }
    return seq;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_del_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {

    /* TINYPY_GRAMMAR_DEL_STMT: 'del' TINYPY_GRAMMAR_EXPRLIST */

    tinypy_ast_sequence_t *expr_list = __ast_for_exprlist(c, TINYPY_CST_CHILD(n, 1), TINYPY_AST_CONTEXT_DELETE);
    if (!expr_list) {
        return NULL;
    }
    tinypy_ast_statement_t return_value_1 = __tinypy_ast_delete(expr_list, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_flow_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_statement_t function_result;
    /* TINYPY_GRAMMAR_FLOW_STMT: TINYPY_GRAMMAR_BREAK_STMT | TINYPY_GRAMMAR_CONTINUE_STMT | TINYPY_GRAMMAR_RETURN_STMT | TINYPY_GRAMMAR_RAISE_STMT
                 | TINYPY_GRAMMAR_YIELD_STMT
      TINYPY_GRAMMAR_BREAK_STMT: 'break'
      TINYPY_GRAMMAR_CONTINUE_STMT: 'continue'
      TINYPY_GRAMMAR_RETURN_STMT: 'return' [TINYPY_GRAMMAR_TESTLIST]
      TINYPY_GRAMMAR_YIELD_STMT: TINYPY_GRAMMAR_YIELD_EXPR
      TINYPY_GRAMMAR_YIELD_EXPR: 'yield' TINYPY_GRAMMAR_TESTLIST
      TINYPY_GRAMMAR_RAISE_STMT: 'raise' [TINYPY_GRAMMAR_TEST [',' TINYPY_GRAMMAR_TEST [',' TINYPY_GRAMMAR_TEST]]]
    */

    tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 0);
    switch (TINYPY_CST_TYPE(ch)) {
    case TINYPY_GRAMMAR_BREAK_STMT:
        function_result = __tinypy_ast_break(TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return function_result;
    case TINYPY_GRAMMAR_CONTINUE_STMT:
        function_result = __tinypy_ast_continue(TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return function_result;
    case TINYPY_GRAMMAR_YIELD_STMT: { /* will reduce to TINYPY_GRAMMAR_YIELD_EXPR */
        tinypy_ast_expression_t exp = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 0));
        if (!exp) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_1 = __tinypy_ast_expr(exp, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_1;
    }
    case TINYPY_GRAMMAR_RETURN_STMT:
        if (TINYPY_CST_CHILD_COUNT(ch) == 1) {
            tinypy_ast_statement_t return_value_2 = __tinypy_ast_return(NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return return_value_2;
        }
        else {
            tinypy_ast_expression_t expression = __ast_for_testlist(c, TINYPY_CST_CHILD(ch, 1));
            if (!expression) {
                return NULL;
            }
            tinypy_ast_statement_t return_value_3 = __tinypy_ast_return(expression, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                   c->c_arena);
            return return_value_3;
        }
    case TINYPY_GRAMMAR_RAISE_STMT:
        if (TINYPY_CST_CHILD_COUNT(ch) == 1) {
            tinypy_ast_statement_t return_value_4 = __tinypy_ast_raise(NULL, NULL, NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                  c->c_arena);
            return return_value_4;
        }
        else if (TINYPY_CST_CHILD_COUNT(ch) == 2) {
            tinypy_ast_expression_t expression = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 1));
            if (!expression) {
                return NULL;
            }
            tinypy_ast_statement_t return_value_5 = __tinypy_ast_raise(expression, NULL, NULL, TINYPY_AST_LINE_NUMBER(n),
                                                  n->column_offset, c->c_arena);
            return return_value_5;
        }
        else if (TINYPY_CST_CHILD_COUNT(ch) == 4) {
            tinypy_ast_expression_t expr1, expr2;

            expr1 = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 1));
            if (!expr1) {
                return NULL;
            }
            expr2 = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 3));
            if (!expr2) {
                return NULL;
            }

            tinypy_ast_statement_t return_value_6 = __tinypy_ast_raise(expr1, expr2, NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                  c->c_arena);
            return return_value_6;
        }
        else if (TINYPY_CST_CHILD_COUNT(ch) == 6) {
            tinypy_ast_expression_t expr1, expr2, expr3;

            expr1 = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 1));
            if (!expr1) {
                return NULL;
            }
            expr2 = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 3));
            if (!expr2) {
                return NULL;
            }
            expr3 = __ast_for_expr(c, TINYPY_CST_CHILD(ch, 5));
            if (!expr3) {
                return NULL;
            }

            tinypy_ast_statement_t return_value_7 = __tinypy_ast_raise(expr1, expr2, expr3, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                                  c->c_arena);
            return return_value_7;
        }
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "unexpected TINYPY_GRAMMAR_RAISE_STMT child count: %d",
                                   TINYPY_CST_CHILD_COUNT(ch));
        return NULL;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "unexpected TINYPY_GRAMMAR_FLOW_STMT: %d", TINYPY_CST_TYPE(ch));
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_alias_t __alias_for_import_name(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, int32_t store) {
    /* TINYPY_GRAMMAR_IMPORT_AS_NAME: TINYPY_TOKEN_NAME ['as' TINYPY_TOKEN_NAME]
      TINYPY_GRAMMAR_DOTTED_AS_NAME: TINYPY_GRAMMAR_DOTTED_NAME ['as' TINYPY_TOKEN_NAME]
      TINYPY_GRAMMAR_DOTTED_NAME: TINYPY_TOKEN_NAME ('.' TINYPY_TOKEN_NAME)*  */
    tinypy_value_t *str, *name;

loop:
    switch (TINYPY_CST_TYPE(n)) {
    case TINYPY_GRAMMAR_IMPORT_AS_NAME: {
        tinypy_cst_node_t *name_node = TINYPY_CST_CHILD(n, 0);
        str = NULL;
        if (TINYPY_CST_CHILD_COUNT(n) == 3) {
            tinypy_cst_node_t *str_node = TINYPY_CST_CHILD(n, 2);
            if (store && !__forbidden_check(c, str_node, TINYPY_CST_TEXT(str_node))) {
                return NULL;
            }
            str = TINYPY_AST_NEW_IDENTIFIER(str_node);
            if (!str) {
                return NULL;
            }
        }
        else {
            if (!__forbidden_check(c, name_node, TINYPY_CST_TEXT(name_node))) {
                return NULL;
            }
        }
        name = TINYPY_AST_NEW_IDENTIFIER(name_node);
        if (!name) {
            return NULL;
        }
        tinypy_ast_alias_t return_value_1 = __tinypy_ast_alias(name, str, c->c_arena);
        return return_value_1;
    }
    case TINYPY_GRAMMAR_DOTTED_AS_NAME:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            n = TINYPY_CST_CHILD(n, 0);
            goto loop;
        }
        else {
            tinypy_cst_node_t *asname_node = TINYPY_CST_CHILD(n, 2);
            tinypy_ast_alias_t a = __alias_for_import_name(c, TINYPY_CST_CHILD(n, 0), 0);
            if (!a) {
                return NULL;
            }
            if (!__forbidden_check(c, asname_node, TINYPY_CST_TEXT(asname_node))) {
                return NULL;
            }
            a->asname = TINYPY_AST_NEW_IDENTIFIER(asname_node);
            if (!a->asname) {
                return NULL;
            }
            return a;
        }
    case TINYPY_GRAMMAR_DOTTED_NAME:
        if (TINYPY_CST_CHILD_COUNT(n) == 1) {
            tinypy_cst_node_t *name_node = TINYPY_CST_CHILD(n, 0);
            if (store && !__forbidden_check(c, name_node, TINYPY_CST_TEXT(name_node))) {
                return NULL;
            }
            name = TINYPY_AST_NEW_IDENTIFIER(name_node);
            if (!name) {
                return NULL;
            }
            tinypy_ast_alias_t return_value_2 = __tinypy_ast_alias(name, NULL, c->c_arena);
            return return_value_2;
        }
        else {
            /* Create a string of the form "a.b.c" */
            int32_t i;
            size_t len;
            char *s;

            len = 0;
            for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
                /* length of string plus one for the dot */
                len += strlen(TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, i))) + 1;
            }
            len--; /* the last name doesn't have a dot */
            s = (char *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena, len + 1U);
            if (!s) {
                return NULL;
            }
            char *write = s;
            for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
                char *sch = TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, i));
                size_t component_size = strlen(sch);
                (void)memcpy(write, sch, component_size);
                write += component_size;
                *write++ = '.';
            }
            write[-1] = '\0';
            str = __new_identifier(s, c->c_arena);
            if (!str) {
                return NULL;
            }
            tinypy_ast_alias_t return_value_3 = __tinypy_ast_alias(str, NULL, c->c_arena);
            return return_value_3;
        }
    case TINYPY_TOKEN_STAR:
        str = __new_identifier("*", c->c_arena);
        if (!str) {
            return NULL;
        }
        tinypy_ast_alias_t return_value_4 = __tinypy_ast_alias(str, NULL, c->c_arena);
        return return_value_4;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "unexpected import name: %d", TINYPY_CST_TYPE(n));
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_import_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_IMPORT_STMT: TINYPY_GRAMMAR_IMPORT_NAME | TINYPY_GRAMMAR_IMPORT_FROM
      TINYPY_GRAMMAR_IMPORT_NAME: 'import' TINYPY_GRAMMAR_DOTTED_AS_NAMES
      TINYPY_GRAMMAR_IMPORT_FROM: 'from' ('.'* TINYPY_GRAMMAR_DOTTED_NAME | '.') 'import'
                          ('*' | '(' TINYPY_GRAMMAR_IMPORT_AS_NAMES ')' | TINYPY_GRAMMAR_IMPORT_AS_NAMES)
    */
    int32_t lineno;
    int32_t col_offset;
    int32_t i;
    tinypy_ast_sequence_t *aliases;

    lineno = TINYPY_AST_LINE_NUMBER(n);
    col_offset = n->column_offset;
    n = TINYPY_CST_CHILD(n, 0);
    if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_IMPORT_NAME) {
        n = TINYPY_CST_CHILD(n, 1);
        aliases = TINYPY_AST_SEQUENCE_NEW((TINYPY_CST_CHILD_COUNT(n) + 1) / 2, c->c_arena);
        if (!aliases) {
            return NULL;
        }
        for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
            tinypy_ast_alias_t import_alias = __alias_for_import_name(c, TINYPY_CST_CHILD(n, i), 1);
            if (!import_alias) {
                return NULL;
            }
            TINYPY_AST_SEQUENCE_SET(aliases, i / 2, import_alias);
        }
        tinypy_ast_statement_t return_value_1 = __tinypy_ast_import(aliases, lineno, col_offset, c->c_arena);
        return return_value_1;
    }
    else if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_IMPORT_FROM) {
        int32_t n_children;
        int32_t idx, ndots = 0;
        tinypy_ast_alias_t mod = NULL;
        tinypy_ast_identifier_t modname = NULL;

        /* Count the number of dots (for relative imports) and check for the
           optional module name */
        for (idx = 1; idx < TINYPY_CST_CHILD_COUNT(n); idx++) {
            if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, idx)) == TINYPY_GRAMMAR_DOTTED_NAME) {
                mod = __alias_for_import_name(c, TINYPY_CST_CHILD(n, idx), 0);
                if (!mod) {
                    return NULL;
                }
                idx++;
                break;
            }
            else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, idx)) != TINYPY_TOKEN_DOT) {
                break;
            }
            ndots++;
        }
        idx++; /* skip over the 'import' keyword */
        switch (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, idx))) {
        case TINYPY_TOKEN_STAR:
            /* from ... import * */
            n = TINYPY_CST_CHILD(n, idx);
            n_children = 1;
            break;
        case TINYPY_TOKEN_LEFT_PARENTHESIS:
            /* from ... import (x, y, z) */
            n = TINYPY_CST_CHILD(n, idx + 1);
            n_children = TINYPY_CST_CHILD_COUNT(n);
            break;
        case TINYPY_GRAMMAR_IMPORT_AS_NAMES:
            /* from ... import x, y, z */
            n = TINYPY_CST_CHILD(n, idx);
            n_children = TINYPY_CST_CHILD_COUNT(n);
            if (n_children % 2 == 0) {
                __ast_error(n, "trailing comma not allowed without"
                               " surrounding parentheses");
                return NULL;
            }
            break;
        default:
            __ast_error(n, "Unexpected tinypy_cst_node_t-type in from-import");
            return NULL;
        }

        aliases = TINYPY_AST_SEQUENCE_NEW((n_children + 1) / 2, c->c_arena);
        if (!aliases) {
            return NULL;
        }

        /* handle "from ... import *" special b/c there's no children */
        if (TINYPY_CST_TYPE(n) == TINYPY_TOKEN_STAR) {
            tinypy_ast_alias_t import_alias = __alias_for_import_name(c, n, 1);
            if (!import_alias) {
                return NULL;
            }
            TINYPY_AST_SEQUENCE_SET(aliases, 0, import_alias);
        }
        else {
            for (i = 0; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
                tinypy_ast_alias_t import_alias = __alias_for_import_name(c, TINYPY_CST_CHILD(n, i), 1);
                if (!import_alias) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(aliases, i / 2, import_alias);
            }
        }
        if (mod != NULL) {
            modname = mod->name;
        }
        tinypy_ast_statement_t return_value_2 = __tinypy_ast_import_from(modname, aliases, ndots, lineno, col_offset,
                                                c->c_arena);
        return return_value_2;
    }
    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                               "unknown import statement: starts with command '%s'",
                               TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 0)));
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_global_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_GLOBAL_STMT: 'global' TINYPY_TOKEN_NAME (',' TINYPY_TOKEN_NAME)* */
    tinypy_ast_identifier_t name;
    int32_t i;

    tinypy_ast_sequence_t *s = TINYPY_AST_SEQUENCE_NEW(TINYPY_CST_CHILD_COUNT(n) / 2, c->c_arena);
    if (!s) {
        return NULL;
    }
    for (i = 1; i < TINYPY_CST_CHILD_COUNT(n); i += 2) {
        name = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, i));
        if (!name) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(s, i / 2, name);
    }
    tinypy_ast_statement_t return_value_1 = __tinypy_ast_global(s, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_exec_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_expression_t expr1, globals = NULL, locals = NULL;
    int32_t n_children = TINYPY_CST_CHILD_COUNT(n);
    if (n_children != 2 && n_children != 4 && n_children != 6) {
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "poorly formed 'exec' statement: %d parts to statement",
                                   n_children);
        return NULL;
    }

    /* TINYPY_GRAMMAR_EXEC_STMT: 'exec' TINYPY_GRAMMAR_EXPR ['in' TINYPY_GRAMMAR_TEST [',' TINYPY_GRAMMAR_TEST]] */
    expr1 = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
    if (!expr1) {
        return NULL;
    }

    if (expr1->kind == TINYPY_AST_KIND_TUPLE && n_children < 4 && (TINYPY_AST_SEQUENCE_LENGTH(expr1->v.Tuple.elts) == 2 || TINYPY_AST_SEQUENCE_LENGTH(expr1->v.Tuple.elts) == 3)) {
        /* Backwards compatibility: passing exec args as a tuple */
        globals = TINYPY_AST_SEQUENCE_GET(expr1->v.Tuple.elts, 1);
        if (TINYPY_AST_SEQUENCE_LENGTH(expr1->v.Tuple.elts) == 3) {
            locals = TINYPY_AST_SEQUENCE_GET(expr1->v.Tuple.elts, 2);
        }
        expr1 = TINYPY_AST_SEQUENCE_GET(expr1->v.Tuple.elts, 0);
    }

    if (n_children >= 4) {
        globals = __ast_for_expr(c, TINYPY_CST_CHILD(n, 3));
        if (!globals) {
            return NULL;
        }
    }
    if (n_children == 6) {
        locals = __ast_for_expr(c, TINYPY_CST_CHILD(n, 5));
        if (!locals) {
            return NULL;
        }
    }

    tinypy_ast_statement_t return_value_1 = __tinypy_ast_exec(expr1, globals, locals, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                 c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_assert_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_ASSERT_STMT: 'assert' TINYPY_GRAMMAR_TEST [',' TINYPY_GRAMMAR_TEST] */
    if (TINYPY_CST_CHILD_COUNT(n) == 2) {
        tinypy_ast_expression_t expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_2 = __tinypy_ast_assert(expression, NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                           c->c_arena);
        return return_value_2;
    }
    else if (TINYPY_CST_CHILD_COUNT(n) == 4) {
        tinypy_ast_expression_t expr1, expr2;

        expr1 = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expr1) {
            return NULL;
        }
        expr2 = __ast_for_expr(c, TINYPY_CST_CHILD(n, 3));
        if (!expr2) {
            return NULL;
        }

        tinypy_ast_statement_t return_value_1 = __tinypy_ast_assert(expr1, expr2, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_1;
    }
    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                               "improper number of parts to 'assert' statement: %d",
                               TINYPY_CST_CHILD_COUNT(n));
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__ast_for_suite(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_SUITE: TINYPY_GRAMMAR_SIMPLE_STMT | TINYPY_TOKEN_NEWLINE TINYPY_TOKEN_INDENT TINYPY_GRAMMAR_STMT+ TINYPY_TOKEN_DEDENT */
    tinypy_ast_statement_t s;
    int32_t i, total, num, end, pos = 0;
    tinypy_cst_node_t *ch;

    total = __num_stmts(n);
    tinypy_ast_sequence_t *seq = TINYPY_AST_SEQUENCE_NEW(total, c->c_arena);
    if (!seq) {
        return NULL;
    }
    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 0)) == TINYPY_GRAMMAR_SIMPLE_STMT) {
        n = TINYPY_CST_CHILD(n, 0);
        /* TINYPY_GRAMMAR_SIMPLE_STMT always ends with a TINYPY_TOKEN_NEWLINE,
           and may have a trailing TINYPY_TOKEN_SEMICOLON
        */
        end = TINYPY_CST_CHILD_COUNT(n) - 1;
        if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, end - 1)) == TINYPY_TOKEN_SEMICOLON) {
            end--;
        }
        /* loop by 2 to skip semi-colons */
        for (i = 0; i < end; i += 2) {
            ch = TINYPY_CST_CHILD(n, i);
            s = __ast_for_stmt(c, ch);
            if (!s) {
                return NULL;
            }
            TINYPY_AST_SEQUENCE_SET(seq, pos++, s);
        }
    }
    else {
        for (i = 2; i < (TINYPY_CST_CHILD_COUNT(n) - 1); i++) {
            ch = TINYPY_CST_CHILD(n, i);
            num = __num_stmts(ch);
            if (num == 1) {
                /* TINYPY_GRAMMAR_SMALL_STMT or TINYPY_GRAMMAR_COMPOUND_STMT with only one child */
                s = __ast_for_stmt(c, ch);
                if (!s) {
                    return NULL;
                }
                TINYPY_AST_SEQUENCE_SET(seq, pos++, s);
            }
            else {
                int32_t j;
                ch = TINYPY_CST_CHILD(ch, 0);
                for (j = 0; j < TINYPY_CST_CHILD_COUNT(ch); j += 2) {
                    /* statement terminates with a semi-colon ';' */
                    if (TINYPY_CST_CHILD_COUNT(TINYPY_CST_CHILD(ch, j)) == 0) {
                        break;
                    }
                    s = __ast_for_stmt(c, TINYPY_CST_CHILD(ch, j));
                    if (!s) {
                        return NULL;
                    }
                    TINYPY_AST_SEQUENCE_SET(seq, pos++, s);
                }
            }
        }
    }
    return seq;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_if_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_IF_STMT: 'if' TINYPY_GRAMMAR_TEST ':' TINYPY_GRAMMAR_SUITE ('elif' TINYPY_GRAMMAR_TEST ':' TINYPY_GRAMMAR_SUITE)* ['else' ':' TINYPY_GRAMMAR_SUITE]
    */
    char *s;

    if (TINYPY_CST_CHILD_COUNT(n) == 4) {
        tinypy_ast_expression_t expression;
        tinypy_ast_sequence_t *suite_seq;

        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }
        suite_seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, 3));
        if (!suite_seq) {
            return NULL;
        }

        tinypy_ast_statement_t return_value_1 = __tinypy_ast_if(expression, suite_seq, NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                       c->c_arena);
        return return_value_1;
    }

    s = TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 4));
    /* s[2], the third character in the string, will be
       's' for el_s_e, or
       'i' for el_i_f
    */
    if (s[2] == 's') {
        tinypy_ast_expression_t expression;
        tinypy_ast_sequence_t *seq1, *seq2;

        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }
        seq1 = __ast_for_suite(c, TINYPY_CST_CHILD(n, 3));
        if (!seq1) {
            return NULL;
        }
        seq2 = __ast_for_suite(c, TINYPY_CST_CHILD(n, 6));
        if (!seq2) {
            return NULL;
        }

        tinypy_ast_statement_t return_value_2 = __tinypy_ast_if(expression, seq1, seq2, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                       c->c_arena);
        return return_value_2;
    }
    else if (s[2] == 'i') {
        int32_t i, n_elif;
        tinypy_bool_t has_else = TINYPY_FALSE;
        tinypy_ast_expression_t expression;
        tinypy_ast_sequence_t *suite_seq;
        tinypy_ast_sequence_t *orelse = NULL;
        n_elif = TINYPY_CST_CHILD_COUNT(n) - 4;
        /* must reference the child n_elif+1 since 'else' token is third,
           not fourth, child from the end. */
        if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, (n_elif + 1))) == TINYPY_TOKEN_NAME && TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, (n_elif + 1)))[2] == 's') {
            has_else = 1;
            n_elif -= 3;
        }
        n_elif /= 4;

        if (has_else) {
            tinypy_ast_sequence_t *suite_seq2;

            orelse = TINYPY_AST_SEQUENCE_NEW(1, c->c_arena);
            if (!orelse) {
                return NULL;
            }
            expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 6));
            if (!expression) {
                return NULL;
            }
            suite_seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 4));
            if (!suite_seq) {
                return NULL;
            }
            suite_seq2 = __ast_for_suite(c, TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1));
            if (!suite_seq2) {
                return NULL;
            }

            TINYPY_AST_SEQUENCE_SET(orelse, 0,
                                    __tinypy_ast_if(expression, suite_seq, suite_seq2,
                                                    TINYPY_AST_LINE_NUMBER(TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 6)),
                                                    TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 6)->column_offset,
                                                    c->c_arena));
            /* the just-created orelse handled the last elif */
            n_elif--;
        }

        for (i = 0; i < n_elif; i++) {
            int32_t off = 5 + (n_elif - i - 1) * 4;
            tinypy_ast_sequence_t *newobj = TINYPY_AST_SEQUENCE_NEW(1, c->c_arena);
            if (!newobj) {
                return NULL;
            }
            expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, off));
            if (!expression) {
                return NULL;
            }
            suite_seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, off + 2));
            if (!suite_seq) {
                return NULL;
            }

            TINYPY_AST_SEQUENCE_SET(newobj, 0,
                                    __tinypy_ast_if(expression, suite_seq, orelse,
                                                    TINYPY_AST_LINE_NUMBER(TINYPY_CST_CHILD(n, off)),
                                                    TINYPY_CST_CHILD(n, off)->column_offset, c->c_arena));
            orelse = newobj;
        }
        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }
        suite_seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, 3));
        if (!suite_seq) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_3 = __tinypy_ast_if(expression, suite_seq, orelse,
                                       TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
        return return_value_3;
    }

    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                               "unexpected token in 'if' statement: %s", s);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_while_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    /* TINYPY_GRAMMAR_WHILE_STMT: 'while' TINYPY_GRAMMAR_TEST ':' TINYPY_GRAMMAR_SUITE ['else' ':' TINYPY_GRAMMAR_SUITE] */

    if (TINYPY_CST_CHILD_COUNT(n) == 4) {
        tinypy_ast_expression_t expression;
        tinypy_ast_sequence_t *suite_seq;

        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }
        suite_seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, 3));
        if (!suite_seq) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_1 = __tinypy_ast_while(expression, suite_seq, NULL, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                          c->c_arena);
        return return_value_1;
    }
    else if (TINYPY_CST_CHILD_COUNT(n) == 7) {
        tinypy_ast_expression_t expression;
        tinypy_ast_sequence_t *seq1, *seq2;

        expression = __ast_for_expr(c, TINYPY_CST_CHILD(n, 1));
        if (!expression) {
            return NULL;
        }
        seq1 = __ast_for_suite(c, TINYPY_CST_CHILD(n, 3));
        if (!seq1) {
            return NULL;
        }
        seq2 = __ast_for_suite(c, TINYPY_CST_CHILD(n, 6));
        if (!seq2) {
            return NULL;
        }

        tinypy_ast_statement_t return_value_2 = __tinypy_ast_while(expression, seq1, seq2, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                          c->c_arena);
        return return_value_2;
    }

    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                               "wrong number of tokens for 'while' statement: %d",
                               TINYPY_CST_CHILD_COUNT(n));
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_for_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_sequence_t *_target, *seq = NULL, *suite_seq;
    tinypy_ast_expression_t expression;
    tinypy_ast_expression_t target, first;
    /* TINYPY_GRAMMAR_FOR_STMT: 'for' TINYPY_GRAMMAR_EXPRLIST 'in' TINYPY_GRAMMAR_TESTLIST ':' TINYPY_GRAMMAR_SUITE ['else' ':' TINYPY_GRAMMAR_SUITE] */

    if (TINYPY_CST_CHILD_COUNT(n) == 9) {
        seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, 8));
        if (!seq) {
            return NULL;
        }
    }

    const tinypy_cst_node_t *node_target = TINYPY_CST_CHILD(n, 1);
    _target = __ast_for_exprlist(c, node_target, TINYPY_AST_CONTEXT_STORE);
    if (!_target) {
        return NULL;
    }
    /* Check the # of children rather than the length of _target, since
       for x, in ... has 1 element in _target, but still requires a Tuple. */
    first = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(_target, 0);
    if (TINYPY_CST_CHILD_COUNT(node_target) == 1) {
        target = first;
    }
    else {
        target = __tinypy_ast_tuple(_target, TINYPY_AST_CONTEXT_STORE, first->lineno, first->col_offset, c->c_arena);
    }

    expression = __ast_for_testlist(c, TINYPY_CST_CHILD(n, 3));
    if (!expression) {
        return NULL;
    }
    suite_seq = __ast_for_suite(c, TINYPY_CST_CHILD(n, 5));
    if (!suite_seq) {
        return NULL;
    }

    tinypy_ast_statement_t return_value_1 = __tinypy_ast_for(target, expression, suite_seq, seq, TINYPY_AST_LINE_NUMBER(n), n->column_offset,
                                c->c_arena);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_exception_handler_t __ast_for_except_clause(tinypy_ast_builder_t *c, const tinypy_cst_node_t *exc, tinypy_cst_node_t *body) {
    /* TINYPY_GRAMMAR_EXCEPT_CLAUSE: 'except' [TINYPY_GRAMMAR_TEST [(',' | 'as') TINYPY_GRAMMAR_TEST]] */

    if (TINYPY_CST_CHILD_COUNT(exc) == 1) {
        tinypy_ast_sequence_t *suite_seq = __ast_for_suite(c, body);
        if (!suite_seq) {
            return NULL;
        }

        tinypy_ast_exception_handler_t return_value_1 = __tinypy_ast_except_handler(NULL, NULL, suite_seq, TINYPY_AST_LINE_NUMBER(exc),
                                                   exc->column_offset, c->c_arena);
        return return_value_1;
    }
    else if (TINYPY_CST_CHILD_COUNT(exc) == 2) {
        tinypy_ast_expression_t expression;
        tinypy_ast_sequence_t *suite_seq;

        expression = __ast_for_expr(c, TINYPY_CST_CHILD(exc, 1));
        if (!expression) {
            return NULL;
        }
        suite_seq = __ast_for_suite(c, body);
        if (!suite_seq) {
            return NULL;
        }

        tinypy_ast_exception_handler_t return_value_2 = __tinypy_ast_except_handler(expression, NULL, suite_seq, TINYPY_AST_LINE_NUMBER(exc),
                                                   exc->column_offset, c->c_arena);
        return return_value_2;
    }
    else if (TINYPY_CST_CHILD_COUNT(exc) == 4) {
        tinypy_ast_sequence_t *suite_seq;
        tinypy_ast_expression_t expression;
        tinypy_ast_expression_t e = __ast_for_expr(c, TINYPY_CST_CHILD(exc, 3));
        if (!e) {
            return NULL;
        }
        if (!__set_context(c, e, TINYPY_AST_CONTEXT_STORE, TINYPY_CST_CHILD(exc, 3))) {
            return NULL;
        }
        expression = __ast_for_expr(c, TINYPY_CST_CHILD(exc, 1));
        if (!expression) {
            return NULL;
        }
        suite_seq = __ast_for_suite(c, body);
        if (!suite_seq) {
            return NULL;
        }

        tinypy_ast_exception_handler_t return_value_3 = __tinypy_ast_except_handler(expression, e, suite_seq, TINYPY_AST_LINE_NUMBER(exc),
                                                   exc->column_offset, c->c_arena);
        return return_value_3;
    }

    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                               "wrong number of children for 'except' clause: %d",
                               TINYPY_CST_CHILD_COUNT(exc));
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_try_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    const int32_t nch = TINYPY_CST_CHILD_COUNT(n);
    int32_t n_except = (nch - 3) / 3;
    tinypy_ast_sequence_t *body, *orelse = NULL, *finally = NULL;

    body = __ast_for_suite(c, TINYPY_CST_CHILD(n, 2));
    if (body == NULL) {
        return NULL;
    }

    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, nch - 3)) == TINYPY_TOKEN_NAME) {
        if (strcmp(TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, nch - 3)), "finally") == 0) {
            if (nch >= 9 && TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, nch - 6)) == TINYPY_TOKEN_NAME) {
                /* we can assume it's an "else",
                   because nch >= 9 for try-else-finally and
                   it would otherwise have a type of TINYPY_GRAMMAR_EXCEPT_CLAUSE */
                orelse = __ast_for_suite(c, TINYPY_CST_CHILD(n, nch - 4));
                if (orelse == NULL) {
                    return NULL;
                }
                n_except--;
            }

            finally = __ast_for_suite(c, TINYPY_CST_CHILD(n, nch - 1));
            if (finally == NULL) {
                return NULL;
            }
            n_except--;
        }
        else {
            /* we can assume it's an "else",
               otherwise it would have a type of TINYPY_GRAMMAR_EXCEPT_CLAUSE */
            orelse = __ast_for_suite(c, TINYPY_CST_CHILD(n, nch - 1));
            if (orelse == NULL) {
                return NULL;
            }
            n_except--;
        }
    }
    else if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, nch - 3)) != TINYPY_GRAMMAR_EXCEPT_CLAUSE) {
        __ast_error(n, "malformed 'try' statement");
        return NULL;
    }

    if (n_except > 0) {
        int32_t i;
        tinypy_ast_statement_t except_st;
        /* process except statements to create a try ... except */
        tinypy_ast_sequence_t *handlers = TINYPY_AST_SEQUENCE_NEW(n_except, c->c_arena);
        if (handlers == NULL) {
            return NULL;
        }

        for (i = 0; i < n_except; i++) {
            tinypy_ast_exception_handler_t e = __ast_for_except_clause(c, TINYPY_CST_CHILD(n, 3 + i * 3),
                                                                       TINYPY_CST_CHILD(n, 5 + i * 3));
            if (!e) {
                return NULL;
            }
            TINYPY_AST_SEQUENCE_SET(handlers, i, e);
        }

        except_st = __tinypy_ast_try_except(body, handlers, orelse, TINYPY_AST_LINE_NUMBER(n),
                                            n->column_offset, c->c_arena);
        if (!finally) {
            return except_st;
        }

        /* if a 'finally' is present too, we nest the TryExcept within a
           TryFinally to emulate try ... except ... finally */
        body = TINYPY_AST_SEQUENCE_NEW(1, c->c_arena);
        if (body == NULL) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(body, 0, except_st);
    }

    /* must be a try ... finally (except clauses are in body, if any exist) */
    tinypy_ast_statement_t return_value_1 = __tinypy_ast_try_finally(body, finally, TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_1;
}

/* TINYPY_GRAMMAR_WITH_ITEM: TINYPY_GRAMMAR_TEST ['as' TINYPY_GRAMMAR_EXPR] */
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_with_item(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, tinypy_ast_sequence_t *content) {
    tinypy_ast_expression_t context_expr, optional_vars = NULL;

    context_expr = __ast_for_expr(c, TINYPY_CST_CHILD(n, 0));
    if (!context_expr) {
        return NULL;
    }
    if (TINYPY_CST_CHILD_COUNT(n) == 3) {
        optional_vars = __ast_for_expr(c, TINYPY_CST_CHILD(n, 2));

        if (!optional_vars) {
            return NULL;
        }
        if (!__set_context(c, optional_vars, TINYPY_AST_CONTEXT_STORE, n)) {
            return NULL;
        }
    }

    tinypy_ast_statement_t return_value_1 = __tinypy_ast_with(context_expr, optional_vars, content, TINYPY_AST_LINE_NUMBER(n),
                                 n->column_offset, c->c_arena);
    return return_value_1;
}

/* TINYPY_GRAMMAR_WITH_STMT: 'with' TINYPY_GRAMMAR_WITH_ITEM (',' TINYPY_GRAMMAR_WITH_ITEM)* ':' TINYPY_GRAMMAR_SUITE */
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_with_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    int32_t i;
    tinypy_ast_statement_t ret;

    /* process the with items inside-out */
    i = TINYPY_CST_CHILD_COUNT(n) - 1;
    /* the TINYPY_GRAMMAR_SUITE of the innermost with item is the TINYPY_GRAMMAR_SUITE of the with TINYPY_GRAMMAR_STMT */
    tinypy_ast_sequence_t *inner = __ast_for_suite(c, TINYPY_CST_CHILD(n, i));
    if (!inner) {
        return NULL;
    }

    for (;;) {
        i -= 2;
        ret = __ast_for_with_item(c, TINYPY_CST_CHILD(n, i), inner);
        if (!ret) {
            return NULL;
        }
        /* was this the last item? */
        if (i == 1) {
            break;
        }
        /* if not, wrap the result so far in a new sequence */
        inner = TINYPY_AST_SEQUENCE_NEW(1, c->c_arena);
        if (!inner) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(inner, 0, ret);
    }

    return ret;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_classdef(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, tinypy_ast_sequence_t *decorator_seq) {
    /* TINYPY_GRAMMAR_CLASSDEF: 'class' TINYPY_TOKEN_NAME ['(' TINYPY_GRAMMAR_TESTLIST ')'] ':' TINYPY_GRAMMAR_SUITE */
    tinypy_ast_sequence_t *bases, *s;

    if (!__forbidden_check(c, n, TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 1)))) {
        return NULL;
    }

    if (TINYPY_CST_CHILD_COUNT(n) == 4) {
        s = __ast_for_suite(c, TINYPY_CST_CHILD(n, 3));
        if (!s) {
            return NULL;
        }
        tinypy_value_t *classname = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, 1));
        if (!classname) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_1 = __tinypy_ast_class_def(classname, NULL, s, decorator_seq, TINYPY_AST_LINE_NUMBER(n),
                                              n->column_offset, c->c_arena);
        return return_value_1;
    }
    /* check for empty base list */
    if (TINYPY_CST_TYPE(TINYPY_CST_CHILD(n, 3)) == TINYPY_TOKEN_RIGHT_PARENTHESIS) {
        s = __ast_for_suite(c, TINYPY_CST_CHILD(n, 5));
        if (!s) {
            return NULL;
        }
        tinypy_value_t *classname = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, 1));
        if (!classname) {
            return NULL;
        }
        tinypy_ast_statement_t return_value_2 = __tinypy_ast_class_def(classname, NULL, s, decorator_seq, TINYPY_AST_LINE_NUMBER(n),
                                              n->column_offset, c->c_arena);
        return return_value_2;
    }

    /* else handle the base class list */
    bases = __ast_for_class_bases(c, TINYPY_CST_CHILD(n, 3));
    if (!bases) {
        return NULL;
    }

    s = __ast_for_suite(c, TINYPY_CST_CHILD(n, 6));
    if (!s) {
        return NULL;
    }
    tinypy_value_t *classname = TINYPY_AST_NEW_IDENTIFIER(TINYPY_CST_CHILD(n, 1));
    if (!classname) {
        return NULL;
    }
    tinypy_ast_statement_t return_value_3 = __tinypy_ast_class_def(classname, bases, s, decorator_seq,
                                      TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __ast_for_stmt(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    tinypy_ast_statement_t function_result;
    if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_STMT) {
        n = TINYPY_CST_CHILD(n, 0);
    }
    if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_SIMPLE_STMT) {
        n = TINYPY_CST_CHILD(n, 0);
    }
    if (TINYPY_CST_TYPE(n) == TINYPY_GRAMMAR_SMALL_STMT) {
        n = TINYPY_CST_CHILD(n, 0);
        /* TINYPY_GRAMMAR_SMALL_STMT: TINYPY_GRAMMAR_EXPR_STMT | TINYPY_GRAMMAR_PRINT_STMT  | TINYPY_GRAMMAR_DEL_STMT | TINYPY_GRAMMAR_PASS_STMT
                     | TINYPY_GRAMMAR_FLOW_STMT | TINYPY_GRAMMAR_IMPORT_STMT | TINYPY_GRAMMAR_GLOBAL_STMT | TINYPY_GRAMMAR_EXEC_STMT
                     | TINYPY_GRAMMAR_ASSERT_STMT
        */
        switch (TINYPY_CST_TYPE(n)) {
        case TINYPY_GRAMMAR_EXPR_STMT:
            function_result = __ast_for_expr_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_PRINT_STMT:
            function_result = __ast_for_print_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_DEL_STMT:
            function_result = __ast_for_del_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_PASS_STMT:
            function_result = __tinypy_ast_pass(TINYPY_AST_LINE_NUMBER(n), n->column_offset, c->c_arena);
            return function_result;
        case TINYPY_GRAMMAR_FLOW_STMT:
            function_result = __ast_for_flow_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_IMPORT_STMT:
            function_result = __ast_for_import_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_GLOBAL_STMT:
            function_result = __ast_for_global_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_EXEC_STMT:
            function_result = __ast_for_exec_stmt(c, n);
            return function_result;
        case TINYPY_GRAMMAR_ASSERT_STMT:
            function_result = __ast_for_assert_stmt(c, n);
            return function_result;
        default:
            TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "unhandled TINYPY_GRAMMAR_SMALL_STMT: TYPE=%d NCH=%d\n",
                                       TINYPY_CST_TYPE(n), TINYPY_CST_CHILD_COUNT(n));
            return NULL;
        }
    }
    else {
        /* TINYPY_GRAMMAR_COMPOUND_STMT: TINYPY_GRAMMAR_IF_STMT | TINYPY_GRAMMAR_WHILE_STMT | TINYPY_GRAMMAR_FOR_STMT | TINYPY_GRAMMAR_TRY_STMT
                        | TINYPY_GRAMMAR_FUNCDEF | TINYPY_GRAMMAR_CLASSDEF | TINYPY_GRAMMAR_DECORATED
        */
        tinypy_cst_node_t *ch = TINYPY_CST_CHILD(n, 0);
        switch (TINYPY_CST_TYPE(ch)) {
        case TINYPY_GRAMMAR_IF_STMT:
            function_result = __ast_for_if_stmt(c, ch);
            return function_result;
        case TINYPY_GRAMMAR_WHILE_STMT:
            function_result = __ast_for_while_stmt(c, ch);
            return function_result;
        case TINYPY_GRAMMAR_FOR_STMT:
            function_result = __ast_for_for_stmt(c, ch);
            return function_result;
        case TINYPY_GRAMMAR_TRY_STMT:
            function_result = __ast_for_try_stmt(c, ch);
            return function_result;
        case TINYPY_GRAMMAR_WITH_STMT:
            function_result = __ast_for_with_stmt(c, ch);
            return function_result;
        case TINYPY_GRAMMAR_FUNCDEF:
            function_result = __ast_for_funcdef(c, ch, NULL);
            return function_result;
        case TINYPY_GRAMMAR_CLASSDEF:
            function_result = __ast_for_classdef(c, ch, NULL);
            return function_result;
        case TINYPY_GRAMMAR_DECORATED:
            function_result = __ast_for_decorated(c, ch);
            return function_result;
        default:
            TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "unhandled TINYPY_GRAMMAR_SMALL_STMT: TYPE=%d NCH=%d\n",
                                       TINYPY_CST_TYPE(n), TINYPY_CST_CHILD_COUNT(n));
            return NULL;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__parsenumber(tinypy_ast_builder_t *c, const char *s) {
    tinypy_value_t *return_value_1 = tinypy_internal_compiler_parse_number(c->c_arena, s, 1, 1);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__parsestr(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n, const char *s) {
    tinypy_value_t *return_value_1 = tinypy_internal_compiler_parse_string(c->c_arena, s, c->c_future_unicode, TINYPY_AST_LINE_NUMBER(n), n->column_offset + 1);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__parsestrplus(tinypy_ast_builder_t *c, const tinypy_cst_node_t *n) {
    int32_t index;

    tinypy_value_t *value = __parsestr(c, n, TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, 0)));
    if (value == NULL) {
        return NULL;
    }
    for (index = 1; index < TINYPY_CST_CHILD_COUNT(n); index++) {
        tinypy_value_t *item = __parsestr(c, n, TINYPY_CST_TEXT(TINYPY_CST_CHILD(n, index)));
        tinypy_value_t *joined;

        if (item == NULL) {
            TINYPY_DECREF(value);
            return NULL;
        }
        joined = tinypy_internal_compiler_concat_strings(c->c_arena, value, item, TINYPY_AST_LINE_NUMBER(n), n->column_offset + 1);
        TINYPY_DECREF(item);
        TINYPY_DECREF(value);
        if (joined == NULL) {
            return NULL;
        }
        value = joined;
    }
    return value;
}
