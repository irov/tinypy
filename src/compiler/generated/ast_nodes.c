/* Immutable TinyPy AST constructors generated from the Python 2.7 grammar. */


/*
   __version__ 82160.

   This module must be committed separately after each AST grammar change;
   The __version__ number is set to the revision number of the commit
   containing the grammar change.
*/

#include "value_ops.h"
#include "ast_nodes.h"

#undef TINYPY_COMPILER_ARENA_MALLOC
#define TINYPY_COMPILER_ARENA_MALLOC(arena, size) tinypy_internal_compiler_ast_allocate((arena), (size))


tinypy_ast_module_t
__tinypy_ast_module(tinypy_ast_sequence_t * body, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_module_t p;
        p = (tinypy_ast_module_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_MODULE;
        p->v.Module.body = body;
        return p;
}

tinypy_ast_module_t
__tinypy_ast_interactive(tinypy_ast_sequence_t * body, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_module_t p;
        p = (tinypy_ast_module_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_INTERACTIVE;
        p->v.Interactive.body = body;
        return p;
}

tinypy_ast_module_t
__tinypy_ast_expression(tinypy_ast_expression_t body, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_module_t p;
        if (!body) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field body is required for Expression");
                return NULL;
        }
        p = (tinypy_ast_module_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_EXPRESSION;
        p->v.Expression.body = body;
        return p;
}

tinypy_ast_module_t
__tinypy_ast_suite(tinypy_ast_sequence_t * body, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_module_t p;
        p = (tinypy_ast_module_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_SUITE;
        p->v.Suite.body = body;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_function_def(tinypy_ast_identifier_t name, tinypy_ast_arguments_t args, tinypy_ast_sequence_t * body, tinypy_ast_sequence_t *
            decorator_list, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!name) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field name is required for FunctionDef");
                return NULL;
        }
        if (!args) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field args is required for FunctionDef");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_FUNCTION_DEF;
        p->v.FunctionDef.name = name;
        p->v.FunctionDef.args = args;
        p->v.FunctionDef.body = body;
        p->v.FunctionDef.decorator_list = decorator_list;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_class_def(tinypy_ast_identifier_t name, tinypy_ast_sequence_t * bases, tinypy_ast_sequence_t * body, tinypy_ast_sequence_t *
         decorator_list, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!name) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field name is required for ClassDef");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_CLASS_DEF;
        p->v.ClassDef.name = name;
        p->v.ClassDef.bases = bases;
        p->v.ClassDef.body = body;
        p->v.ClassDef.decorator_list = decorator_list;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_return(tinypy_ast_expression_t value, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_RETURN;
        p->v.Return.value = value;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_delete(tinypy_ast_sequence_t * targets, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_DELETE;
        p->v.Delete.targets = targets;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_assign(tinypy_ast_sequence_t * targets, tinypy_ast_expression_t value, int lineno, int col_offset, tinypy_compile_ctx_t
       *arena)
{
        tinypy_ast_statement_t p;
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for Assign");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_ASSIGN;
        p->v.Assign.targets = targets;
        p->v.Assign.value = value;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_aug_assign(tinypy_ast_expression_t target, tinypy_ast_binary_operator_e op, tinypy_ast_expression_t value, int lineno, int
          col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!target) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field target is required for AugAssign");
                return NULL;
        }
        if (!op) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field op is required for AugAssign");
                return NULL;
        }
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for AugAssign");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_AUG_ASSIGN;
        p->v.AugAssign.target = target;
        p->v.AugAssign.op = op;
        p->v.AugAssign.value = value;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_print(tinypy_ast_expression_t dest, tinypy_ast_sequence_t * values, tinypy_compiler_boolean_e nl, int lineno, int col_offset,
      tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_PRINT;
        p->v.Print.dest = dest;
        p->v.Print.values = values;
        p->v.Print.nl = nl;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_for(tinypy_ast_expression_t target, tinypy_ast_expression_t iter, tinypy_ast_sequence_t * body, tinypy_ast_sequence_t * orelse, int
    lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!target) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field target is required for For");
                return NULL;
        }
        if (!iter) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field iter is required for For");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_FOR;
        p->v.For.target = target;
        p->v.For.iter = iter;
        p->v.For.body = body;
        p->v.For.orelse = orelse;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_while(tinypy_ast_expression_t test, tinypy_ast_sequence_t * body, tinypy_ast_sequence_t * orelse, int lineno, int
      col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!test) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field test is required for While");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_WHILE;
        p->v.While.test = test;
        p->v.While.body = body;
        p->v.While.orelse = orelse;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_if(tinypy_ast_expression_t test, tinypy_ast_sequence_t * body, tinypy_ast_sequence_t * orelse, int lineno, int
   col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!test) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field test is required for If");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_IF;
        p->v.If.test = test;
        p->v.If.body = body;
        p->v.If.orelse = orelse;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_with(tinypy_ast_expression_t context_expr, tinypy_ast_expression_t optional_vars, tinypy_ast_sequence_t * body, int lineno,
     int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!context_expr) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field context_expr is required for With");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_WITH;
        p->v.With.context_expr = context_expr;
        p->v.With.optional_vars = optional_vars;
        p->v.With.body = body;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_raise(tinypy_ast_expression_t type, tinypy_ast_expression_t inst, tinypy_ast_expression_t tback, int lineno, int col_offset,
      tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_RAISE;
        p->v.Raise.type = type;
        p->v.Raise.inst = inst;
        p->v.Raise.tback = tback;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_try_except(tinypy_ast_sequence_t * body, tinypy_ast_sequence_t * handlers, tinypy_ast_sequence_t * orelse, int lineno,
          int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_TRY_EXCEPT;
        p->v.TryExcept.body = body;
        p->v.TryExcept.handlers = handlers;
        p->v.TryExcept.orelse = orelse;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_try_finally(tinypy_ast_sequence_t * body, tinypy_ast_sequence_t * finalbody, int lineno, int col_offset,
           tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_TRY_FINALLY;
        p->v.TryFinally.body = body;
        p->v.TryFinally.finalbody = finalbody;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_assert(tinypy_ast_expression_t test, tinypy_ast_expression_t msg, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!test) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field test is required for Assert");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_ASSERT;
        p->v.Assert.test = test;
        p->v.Assert.msg = msg;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_import(tinypy_ast_sequence_t * names, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_IMPORT;
        p->v.Import.names = names;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_import_from(tinypy_ast_identifier_t module, tinypy_ast_sequence_t * names, int level, int lineno, int
           col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_IMPORT_FROM;
        p->v.ImportFrom.module = module;
        p->v.ImportFrom.names = names;
        p->v.ImportFrom.level = level;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_exec(tinypy_ast_expression_t body, tinypy_ast_expression_t globals, tinypy_ast_expression_t locals, int lineno, int col_offset,
     tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!body) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field body is required for Exec");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_EXEC;
        p->v.Exec.body = body;
        p->v.Exec.globals = globals;
        p->v.Exec.locals = locals;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_global(tinypy_ast_sequence_t * names, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_GLOBAL;
        p->v.Global.names = names;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_expr(tinypy_ast_expression_t value, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for Expr");
                return NULL;
        }
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_EXPR;
        p->v.Expr.value = value;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_pass(int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_PASS;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_break(int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_BREAK;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_statement_t
__tinypy_ast_continue(int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_statement_t p;
        p = (tinypy_ast_statement_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_CONTINUE;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_bool_op(tinypy_ast_boolean_operator_e op, tinypy_ast_sequence_t * values, int lineno, int col_offset, tinypy_compile_ctx_t
       *arena)
{
        tinypy_ast_expression_t p;
        if (!op) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field op is required for BoolOp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_BOOL_OP;
        p->v.BoolOp.op = op;
        p->v.BoolOp.values = values;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_bin_op(tinypy_ast_expression_t left, tinypy_ast_binary_operator_e op, tinypy_ast_expression_t right, int lineno, int col_offset,
      tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!left) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field left is required for BinOp");
                return NULL;
        }
        if (!op) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field op is required for BinOp");
                return NULL;
        }
        if (!right) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field right is required for BinOp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_BIN_OP;
        p->v.BinOp.left = left;
        p->v.BinOp.op = op;
        p->v.BinOp.right = right;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_unary_op(tinypy_ast_unary_operator_e op, tinypy_ast_expression_t operand, int lineno, int col_offset, tinypy_compile_ctx_t
        *arena)
{
        tinypy_ast_expression_t p;
        if (!op) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field op is required for UnaryOp");
                return NULL;
        }
        if (!operand) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field operand is required for UnaryOp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_UNARY_OP;
        p->v.UnaryOp.op = op;
        p->v.UnaryOp.operand = operand;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_lambda(tinypy_ast_arguments_t args, tinypy_ast_expression_t body, int lineno, int col_offset, tinypy_compile_ctx_t
       *arena)
{
        tinypy_ast_expression_t p;
        if (!args) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field args is required for Lambda");
                return NULL;
        }
        if (!body) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field body is required for Lambda");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_LAMBDA;
        p->v.Lambda.args = args;
        p->v.Lambda.body = body;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_if_exp(tinypy_ast_expression_t test, tinypy_ast_expression_t body, tinypy_ast_expression_t orelse, int lineno, int col_offset,
      tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!test) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field test is required for IfExp");
                return NULL;
        }
        if (!body) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field body is required for IfExp");
                return NULL;
        }
        if (!orelse) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field orelse is required for IfExp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_IF_EXP;
        p->v.IfExp.test = test;
        p->v.IfExp.body = body;
        p->v.IfExp.orelse = orelse;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_dict(tinypy_ast_sequence_t * keys, tinypy_ast_sequence_t * values, int lineno, int col_offset, tinypy_compile_ctx_t
     *arena)
{
        tinypy_ast_expression_t p;
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_DICT;
        p->v.Dict.keys = keys;
        p->v.Dict.values = values;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_set(tinypy_ast_sequence_t * elts, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_SET;
        p->v.Set.elts = elts;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_list_comp(tinypy_ast_expression_t elt, tinypy_ast_sequence_t * generators, int lineno, int col_offset,
         tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!elt) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field elt is required for ListComp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_LIST_COMP;
        p->v.ListComp.elt = elt;
        p->v.ListComp.generators = generators;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_set_comp(tinypy_ast_expression_t elt, tinypy_ast_sequence_t * generators, int lineno, int col_offset, tinypy_compile_ctx_t
        *arena)
{
        tinypy_ast_expression_t p;
        if (!elt) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field elt is required for SetComp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_SET_COMP;
        p->v.SetComp.elt = elt;
        p->v.SetComp.generators = generators;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_dict_comp(tinypy_ast_expression_t key, tinypy_ast_expression_t value, tinypy_ast_sequence_t * generators, int lineno, int
         col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!key) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field key is required for DictComp");
                return NULL;
        }
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for DictComp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_DICT_COMP;
        p->v.DictComp.key = key;
        p->v.DictComp.value = value;
        p->v.DictComp.generators = generators;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_generator_exp(tinypy_ast_expression_t elt, tinypy_ast_sequence_t * generators, int lineno, int col_offset,
             tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!elt) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field elt is required for GeneratorExp");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_GENERATOR_EXP;
        p->v.GeneratorExp.elt = elt;
        p->v.GeneratorExp.generators = generators;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_yield(tinypy_ast_expression_t value, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_YIELD;
        p->v.Yield.value = value;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_compare(tinypy_ast_expression_t left, tinypy_ast_integer_sequence_t * ops, tinypy_ast_sequence_t * comparators, int lineno,
        int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!left) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field left is required for Compare");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_COMPARE;
        p->v.Compare.left = left;
        p->v.Compare.ops = ops;
        p->v.Compare.comparators = comparators;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_call(tinypy_ast_expression_t func, tinypy_ast_sequence_t * args, tinypy_ast_sequence_t * keywords, tinypy_ast_expression_t starargs,
     tinypy_ast_expression_t kwargs, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!func) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field func is required for Call");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_CALL;
        p->v.Call.func = func;
        p->v.Call.args = args;
        p->v.Call.keywords = keywords;
        p->v.Call.starargs = starargs;
        p->v.Call.kwargs = kwargs;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_repr(tinypy_ast_expression_t value, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for Repr");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_REPR;
        p->v.Repr.value = value;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_num(tinypy_ast_literal_t n, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!n) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field n is required for Num");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_NUM;
        p->v.Num.n = n;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_str(tinypy_ast_string_t s, int lineno, int col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!s) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field s is required for Str");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_STR;
        p->v.Str.s = s;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_attribute(tinypy_ast_expression_t value, tinypy_ast_identifier_t attr, tinypy_ast_expression_context_e ctx, int lineno, int
          col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for Attribute");
                return NULL;
        }
        if (!attr) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field attr is required for Attribute");
                return NULL;
        }
        if (!ctx) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field ctx is required for Attribute");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_ATTRIBUTE;
        p->v.Attribute.value = value;
        p->v.Attribute.attr = attr;
        p->v.Attribute.ctx = ctx;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_subscript(tinypy_ast_expression_t value, tinypy_ast_slice_t slice, tinypy_ast_expression_context_e ctx, int lineno, int
          col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_expression_t p;
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for Subscript");
                return NULL;
        }
        if (!slice) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field slice is required for Subscript");
                return NULL;
        }
        if (!ctx) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field ctx is required for Subscript");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_SUBSCRIPT;
        p->v.Subscript.value = value;
        p->v.Subscript.slice = slice;
        p->v.Subscript.ctx = ctx;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_name(tinypy_ast_identifier_t id, tinypy_ast_expression_context_e ctx, int lineno, int col_offset, tinypy_compile_ctx_t
     *arena)
{
        tinypy_ast_expression_t p;
        if (!id) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field id is required for Name");
                return NULL;
        }
        if (!ctx) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field ctx is required for Name");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_NAME;
        p->v.Name.id = id;
        p->v.Name.ctx = ctx;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_list(tinypy_ast_sequence_t * elts, tinypy_ast_expression_context_e ctx, int lineno, int col_offset, tinypy_compile_ctx_t
     *arena)
{
        tinypy_ast_expression_t p;
        if (!ctx) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field ctx is required for List");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_LIST;
        p->v.List.elts = elts;
        p->v.List.ctx = ctx;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_expression_t
__tinypy_ast_tuple(tinypy_ast_sequence_t * elts, tinypy_ast_expression_context_e ctx, int lineno, int col_offset, tinypy_compile_ctx_t
      *arena)
{
        tinypy_ast_expression_t p;
        if (!ctx) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field ctx is required for Tuple");
                return NULL;
        }
        p = (tinypy_ast_expression_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_TUPLE;
        p->v.Tuple.elts = elts;
        p->v.Tuple.ctx = ctx;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_slice_t
__tinypy_ast_ellipsis(tinypy_compile_ctx_t *arena)
{
        tinypy_ast_slice_t p;
        p = (tinypy_ast_slice_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_ELLIPSIS;
        return p;
}

tinypy_ast_slice_t
__tinypy_ast_slice(tinypy_ast_expression_t lower, tinypy_ast_expression_t upper, tinypy_ast_expression_t step, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_slice_t p;
        p = (tinypy_ast_slice_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_SLICE;
        p->v.Slice.lower = lower;
        p->v.Slice.upper = upper;
        p->v.Slice.step = step;
        return p;
}

tinypy_ast_slice_t
__tinypy_ast_ext_slice(tinypy_ast_sequence_t * dims, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_slice_t p;
        p = (tinypy_ast_slice_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_EXT_SLICE;
        p->v.ExtSlice.dims = dims;
        return p;
}

tinypy_ast_slice_t
__tinypy_ast_index(tinypy_ast_expression_t value, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_slice_t p;
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for Index");
                return NULL;
        }
        p = (tinypy_ast_slice_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_INDEX;
        p->v.Index.value = value;
        return p;
}

tinypy_ast_comprehension_t
__tinypy_ast_comprehension(tinypy_ast_expression_t target, tinypy_ast_expression_t iter, tinypy_ast_sequence_t * ifs, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_comprehension_t p;
        if (!target) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field target is required for comprehension");
                return NULL;
        }
        if (!iter) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field iter is required for comprehension");
                return NULL;
        }
        p = (tinypy_ast_comprehension_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->target = target;
        p->iter = iter;
        p->ifs = ifs;
        return p;
}

tinypy_ast_exception_handler_t
__tinypy_ast_except_handler(tinypy_ast_expression_t type, tinypy_ast_expression_t name, tinypy_ast_sequence_t * body, int lineno, int
              col_offset, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_exception_handler_t p;
        p = (tinypy_ast_exception_handler_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->kind = TINYPY_AST_KIND_EXCEPT_HANDLER;
        p->v.ExceptHandler.type = type;
        p->v.ExceptHandler.name = name;
        p->v.ExceptHandler.body = body;
        p->lineno = lineno;
        p->col_offset = col_offset;
        return p;
}

tinypy_ast_arguments_t
__tinypy_ast_arguments(tinypy_ast_sequence_t * args, tinypy_ast_identifier_t vararg, tinypy_ast_identifier_t kwarg, tinypy_ast_sequence_t *
          defaults, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_arguments_t p;
        p = (tinypy_ast_arguments_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->args = args;
        p->vararg = vararg;
        p->kwarg = kwarg;
        p->defaults = defaults;
        return p;
}

tinypy_ast_keyword_t
__tinypy_ast_keyword(tinypy_ast_identifier_t arg, tinypy_ast_expression_t value, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_keyword_t p;
        if (!arg) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field arg is required for keyword");
                return NULL;
        }
        if (!value) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field value is required for keyword");
                return NULL;
        }
        p = (tinypy_ast_keyword_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->arg = arg;
        p->value = value;
        return p;
}

tinypy_ast_alias_t
__tinypy_ast_alias(tinypy_ast_identifier_t name, tinypy_ast_identifier_t asname, tinypy_compile_ctx_t *arena)
{
        tinypy_ast_alias_t p;
        if (!name) {
                TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_VALUE_ERROR,
                                "field name is required for alias");
                return NULL;
        }
        p = (tinypy_ast_alias_t)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(*p));
        if (!p)
                return NULL;
        p->name = name;
        p->asname = asname;
        return p;
}
