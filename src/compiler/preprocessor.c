#include "internal.h"

#include "ast_nodes.h"
#include "value_ops.h"

#include "tinypy/comparison.h"
#include "tinypy/dict.h"
#include "tinypy/list.h"
#include "tinypy/long.h"
#include "tinypy/numeric.h"
#include "tinypy/operator.h"
#include "tinypy/set.h"
#include "tinypy/tuple.h"
#include "tinypy/value.h"

#include <limits.h>
#include <string.h>

typedef struct tinypy_preprocessor_sequence_node_t {
    struct tinypy_preprocessor_sequence_node_t *next;
    tinypy_ast_statement_t statement;
} tinypy_preprocessor_sequence_node_t;

typedef struct tinypy_preprocessor_sequence_builder_t {
    tinypy_preprocessor_sequence_node_t *head;
    tinypy_preprocessor_sequence_node_t *tail;
    size_t size;
} tinypy_preprocessor_sequence_builder_t;

static int32_t __tinypy_preprocessor_expression_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_expression_t expression);
static int32_t __tinypy_preprocessor_statement_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_statement_t statement);
static tinypy_ast_expression_t __tinypy_preprocessor_expression_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_expression_t expression);
static tinypy_ast_sequence_t *__tinypy_preprocessor_sequence_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *sequence);

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_identifier_equal(tinypy_ast_identifier_t identifier, const char *name, size_t name_size) {
    size_t identifier_size;
    const char *identifier_data;

    if (identifier == NULL) {
        return 0;
    }
    identifier_data = (const char *)tinypy_string_view(identifier, &identifier_size);
    return identifier_size == name_size && (name_size == 0U || memcmp(identifier_data, name, name_size) == 0) ? 1 : 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_identifier_reserved(tinypy_ast_identifier_t identifier) {
    size_t size;
    const char *data;

    if (identifier == NULL) {
        return 0;
    }
    data = (const char *)tinypy_string_view(identifier, &size);
    return tinypy_preprocessor_name_is_reserved(data, size) != 0 ? 1 : 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_preprocessor_identifier_error(tinypy_compile_ctx_t *ctx, tinypy_ast_identifier_t identifier, const char *suffix, int32_t line, int32_t column) {
    static const char prefix[] = "build constant ";
    const char *parts[3];
    size_t sizes[3];

    parts[0] = prefix;
    sizes[0] = sizeof(prefix) - 1U;
    parts[1] = (const char *)tinypy_string_view(identifier, &sizes[1]);
    parts[2] = suffix;
    sizes[2] = strlen(suffix);
    tinypy_internal_compiler_error_parts(ctx, TINYPY_ERROR_PREPROCESSOR, parts, sizes, 3U, line, column + 1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_tick(tinypy_compile_ctx_t *ctx, int32_t line, int32_t column) {
    if (ctx->limits.max_preprocessor_operations != 0U && ctx->preprocessor_operations >= ctx->limits.max_preprocessor_operations) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "preprocessor operation limit exceeded", line, column + 1, ctx->out_error);
        return 0;
    }
    ctx->preprocessor_operations += 1U;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_value_tick(tinypy_compile_ctx_t *ctx, int32_t line, int32_t column) {
    if (ctx->limits.max_preprocessor_value_nodes != 0U && ctx->preprocessor_value_nodes >= ctx->limits.max_preprocessor_value_nodes) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "preprocessor value limit exceeded", line, column + 1, ctx->out_error);
        return 0;
    }
    ctx->preprocessor_value_nodes += 1U;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_bytes_add(tinypy_compile_ctx_t *ctx, size_t size, int32_t line, int32_t column) {
    if (size > SIZE_MAX - ctx->preprocessor_bytes || (ctx->limits.max_preprocessor_bytes != 0U && (ctx->preprocessor_bytes > ctx->limits.max_preprocessor_bytes || size > ctx->limits.max_preprocessor_bytes - ctx->preprocessor_bytes))) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "preprocessor byte limit exceeded", line, column + 1, ctx->out_error);
        return 0;
    }
    ctx->preprocessor_bytes += size;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_identifier_validate_binding(tinypy_compile_ctx_t *ctx, tinypy_ast_identifier_t identifier, int32_t line, int32_t column) {
    if (__tinypy_preprocessor_identifier_reserved(identifier) == 0) {
        return 1;
    }
    __tinypy_preprocessor_identifier_error(ctx, identifier, " cannot be rebound", line, column);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_expression_sequence_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *sequence) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (__tinypy_preprocessor_expression_validate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(sequence, index)) == 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_statement_sequence_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *sequence) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (__tinypy_preprocessor_statement_validate(ctx, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(sequence, index)) == 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_slice_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_slice_t slice) {
    int index;

    if (slice == NULL) {
        return 1;
    }
    switch (slice->kind) {
    case TINYPY_AST_KIND_ELLIPSIS:
        return 1;
    case TINYPY_AST_KIND_SLICE:
        return (slice->v.Slice.lower == NULL || __tinypy_preprocessor_expression_validate(ctx, slice->v.Slice.lower) != 0) && (slice->v.Slice.upper == NULL || __tinypy_preprocessor_expression_validate(ctx, slice->v.Slice.upper) != 0) && (slice->v.Slice.step == NULL || __tinypy_preprocessor_expression_validate(ctx, slice->v.Slice.step) != 0);
    case TINYPY_AST_KIND_EXT_SLICE:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(slice->v.ExtSlice.dims); ++index) {
            if (__tinypy_preprocessor_slice_validate(ctx, (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(slice->v.ExtSlice.dims, index)) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_INDEX:
        return __tinypy_preprocessor_expression_validate(ctx, slice->v.Index.value);
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_arguments_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_arguments_t arguments, int32_t line, int32_t column) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(arguments->args); ++index) {
        if (__tinypy_preprocessor_expression_validate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(arguments->args, index)) == 0) {
            return 0;
        }
    }
    if (__tinypy_preprocessor_identifier_validate_binding(ctx, arguments->vararg, line, column) == 0 || __tinypy_preprocessor_identifier_validate_binding(ctx, arguments->kwarg, line, column) == 0) {
        return 0;
    }
    return __tinypy_preprocessor_expression_sequence_validate(ctx, arguments->defaults);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_comprehensions_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *generators) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(generators); ++index) {
        tinypy_ast_comprehension_t generator = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, index);
        if (__tinypy_preprocessor_expression_validate(ctx, generator->target) == 0 || __tinypy_preprocessor_expression_validate(ctx, generator->iter) == 0 || __tinypy_preprocessor_expression_sequence_validate(ctx, generator->ifs) == 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_expression_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_expression_t expression) {
    int index;

    if (expression == NULL || ctx->failed != 0) {
        return expression == NULL ? 1 : 0;
    }
    if (__tinypy_preprocessor_tick(ctx, expression->lineno, expression->col_offset) == 0) {
        return 0;
    }
    switch (expression->kind) {
    case TINYPY_AST_KIND_NAME:
        if (expression->v.Name.ctx != TINYPY_AST_CONTEXT_LOAD) {
            return __tinypy_preprocessor_identifier_validate_binding(ctx, expression->v.Name.id, expression->lineno, expression->col_offset);
        }
        if (__tinypy_preprocessor_identifier_reserved(expression->v.Name.id) != 0) {
            const tinypy_build_value_t *value;
            size_t size;
            const char *name = (const char *)tinypy_string_view(expression->v.Name.id, &size);

            if (tinypy_build_profile_find(ctx->options.build_profile, name, size, &value) == 0) {
                (void)value;
                __tinypy_preprocessor_identifier_error(ctx, expression->v.Name.id, " is not defined by the build profile", expression->lineno, expression->col_offset);
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
        return 1;
    case TINYPY_AST_KIND_BOOL_OP:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.BoolOp.values);
    case TINYPY_AST_KIND_BIN_OP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.BinOp.left) && __tinypy_preprocessor_expression_validate(ctx, expression->v.BinOp.right);
    case TINYPY_AST_KIND_UNARY_OP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.UnaryOp.operand);
    case TINYPY_AST_KIND_LAMBDA:
        return __tinypy_preprocessor_arguments_validate(ctx, expression->v.Lambda.args, expression->lineno, expression->col_offset) && __tinypy_preprocessor_expression_validate(ctx, expression->v.Lambda.body);
    case TINYPY_AST_KIND_IF_EXP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.IfExp.test) && __tinypy_preprocessor_expression_validate(ctx, expression->v.IfExp.body) && __tinypy_preprocessor_expression_validate(ctx, expression->v.IfExp.orelse);
    case TINYPY_AST_KIND_DICT:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.Dict.keys) && __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.Dict.values);
    case TINYPY_AST_KIND_SET:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.Set.elts);
    case TINYPY_AST_KIND_LIST_COMP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.ListComp.elt) && __tinypy_preprocessor_comprehensions_validate(ctx, expression->v.ListComp.generators);
    case TINYPY_AST_KIND_SET_COMP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.SetComp.elt) && __tinypy_preprocessor_comprehensions_validate(ctx, expression->v.SetComp.generators);
    case TINYPY_AST_KIND_DICT_COMP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.DictComp.key) && __tinypy_preprocessor_expression_validate(ctx, expression->v.DictComp.value) && __tinypy_preprocessor_comprehensions_validate(ctx, expression->v.DictComp.generators);
    case TINYPY_AST_KIND_GENERATOR_EXP:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.GeneratorExp.elt) && __tinypy_preprocessor_comprehensions_validate(ctx, expression->v.GeneratorExp.generators);
    case TINYPY_AST_KIND_YIELD:
        return expression->v.Yield.value == NULL || __tinypy_preprocessor_expression_validate(ctx, expression->v.Yield.value);
    case TINYPY_AST_KIND_COMPARE:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.Compare.left) && __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.Compare.comparators);
    case TINYPY_AST_KIND_CALL:
        if (__tinypy_preprocessor_expression_validate(ctx, expression->v.Call.func) == 0 || __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.Call.args) == 0) {
            return 0;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords); ++index) {
            tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.keywords, index);
            if (__tinypy_preprocessor_expression_validate(ctx, keyword->value) == 0) {
                return 0;
            }
        }
        return (expression->v.Call.starargs == NULL || __tinypy_preprocessor_expression_validate(ctx, expression->v.Call.starargs) != 0) && (expression->v.Call.kwargs == NULL || __tinypy_preprocessor_expression_validate(ctx, expression->v.Call.kwargs) != 0);
    case TINYPY_AST_KIND_REPR:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.Repr.value);
    case TINYPY_AST_KIND_ATTRIBUTE:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.Attribute.value);
    case TINYPY_AST_KIND_SUBSCRIPT:
        return __tinypy_preprocessor_expression_validate(ctx, expression->v.Subscript.value) && __tinypy_preprocessor_slice_validate(ctx, expression->v.Subscript.slice);
    case TINYPY_AST_KIND_LIST:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.List.elts);
    case TINYPY_AST_KIND_TUPLE:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, expression->v.Tuple.elts);
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_alias_binding_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_alias_t alias, int32_t line, int32_t column) {
    tinypy_ast_identifier_t binding = alias->asname;
    const char *data;
    size_t size;
    size_t dot = 0U;

    if (binding != NULL) {
        return __tinypy_preprocessor_identifier_validate_binding(ctx, binding, line, column);
    }
    data = (const char *)tinypy_string_view(alias->name, &size);
    while (dot < size && data[dot] != '.') {
        dot += 1U;
    }
    if (tinypy_preprocessor_name_is_reserved(data, dot) == 0) {
        return 1;
    }
    __tinypy_preprocessor_identifier_error(ctx, alias->name, " cannot be rebound by import", line, column);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_statement_validate(tinypy_compile_ctx_t *ctx, tinypy_ast_statement_t statement) {
    int index;

    if (ctx->failed != 0) {
        return 0;
    }
    if (__tinypy_preprocessor_tick(ctx, statement->lineno, statement->col_offset) == 0) {
        return 0;
    }
    switch (statement->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        if (__tinypy_preprocessor_identifier_validate_binding(ctx, statement->v.FunctionDef.name, statement->lineno, statement->col_offset) == 0) {
            return 0;
        }
        return __tinypy_preprocessor_arguments_validate(ctx, statement->v.FunctionDef.args, statement->lineno, statement->col_offset) && __tinypy_preprocessor_expression_sequence_validate(ctx, statement->v.FunctionDef.decorator_list) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.FunctionDef.body);
    case TINYPY_AST_KIND_CLASS_DEF:
        if (__tinypy_preprocessor_identifier_validate_binding(ctx, statement->v.ClassDef.name, statement->lineno, statement->col_offset) == 0) {
            return 0;
        }
        return __tinypy_preprocessor_expression_sequence_validate(ctx, statement->v.ClassDef.bases) && __tinypy_preprocessor_expression_sequence_validate(ctx, statement->v.ClassDef.decorator_list) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.ClassDef.body);
    case TINYPY_AST_KIND_RETURN:
        return statement->v.Return.value == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Return.value);
    case TINYPY_AST_KIND_DELETE:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, statement->v.Delete.targets);
    case TINYPY_AST_KIND_ASSIGN:
        return __tinypy_preprocessor_expression_sequence_validate(ctx, statement->v.Assign.targets) && __tinypy_preprocessor_expression_validate(ctx, statement->v.Assign.value);
    case TINYPY_AST_KIND_AUG_ASSIGN:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.AugAssign.target) && __tinypy_preprocessor_expression_validate(ctx, statement->v.AugAssign.value);
    case TINYPY_AST_KIND_PRINT:
        return (statement->v.Print.dest == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Print.dest) != 0) && __tinypy_preprocessor_expression_sequence_validate(ctx, statement->v.Print.values);
    case TINYPY_AST_KIND_FOR:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.For.target) && __tinypy_preprocessor_expression_validate(ctx, statement->v.For.iter) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.For.body) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.For.orelse);
    case TINYPY_AST_KIND_WHILE:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.While.test) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.While.body) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.While.orelse);
    case TINYPY_AST_KIND_IF:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.If.test) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.If.body) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.If.orelse);
    case TINYPY_AST_KIND_WITH:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.With.context_expr) && (statement->v.With.optional_vars == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.With.optional_vars) != 0) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.With.body);
    case TINYPY_AST_KIND_RAISE:
        return (statement->v.Raise.type == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Raise.type) != 0) && (statement->v.Raise.inst == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Raise.inst) != 0) && (statement->v.Raise.tback == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Raise.tback) != 0);
    case TINYPY_AST_KIND_TRY_EXCEPT:
        if (__tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.TryExcept.body) == 0 || __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.TryExcept.orelse) == 0) {
            return 0;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.handlers); ++index) {
            tinypy_ast_exception_handler_t handler = (tinypy_ast_exception_handler_t)TINYPY_AST_SEQUENCE_GET(statement->v.TryExcept.handlers, index);
            if ((handler->v.ExceptHandler.type != NULL && __tinypy_preprocessor_expression_validate(ctx, handler->v.ExceptHandler.type) == 0) || (handler->v.ExceptHandler.name != NULL && __tinypy_preprocessor_expression_validate(ctx, handler->v.ExceptHandler.name) == 0) || __tinypy_preprocessor_statement_sequence_validate(ctx, handler->v.ExceptHandler.body) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_TRY_FINALLY:
        return __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.TryFinally.body) && __tinypy_preprocessor_statement_sequence_validate(ctx, statement->v.TryFinally.finalbody);
    case TINYPY_AST_KIND_ASSERT:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.Assert.test) && (statement->v.Assert.msg == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Assert.msg) != 0);
    case TINYPY_AST_KIND_IMPORT:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.Import.names); ++index) {
            if (__tinypy_preprocessor_alias_binding_validate(ctx, (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(statement->v.Import.names, index), statement->lineno, statement->col_offset) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_IMPORT_FROM:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.ImportFrom.names); ++index) {
            if (__tinypy_preprocessor_alias_binding_validate(ctx, (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(statement->v.ImportFrom.names, index), statement->lineno, statement->col_offset) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_EXEC:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.Exec.body) && (statement->v.Exec.globals == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Exec.globals) != 0) && (statement->v.Exec.locals == NULL || __tinypy_preprocessor_expression_validate(ctx, statement->v.Exec.locals) != 0);
    case TINYPY_AST_KIND_GLOBAL:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.Global.names); ++index) {
            if (__tinypy_preprocessor_identifier_validate_binding(ctx, (tinypy_ast_identifier_t)TINYPY_AST_SEQUENCE_GET(statement->v.Global.names, index), statement->lineno, statement->col_offset) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_EXPR:
        return __tinypy_preprocessor_expression_validate(ctx, statement->v.Expr.value);
    case TINYPY_AST_KIND_PASS:
    case TINYPY_AST_KIND_BREAK:
    case TINYPY_AST_KIND_CONTINUE:
        return 1;
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_preprocessor_build_value(tinypy_compile_ctx_t *ctx, const tinypy_build_value_t *value) {
    tinypy_value_t *result;
    size_t payload_size = sizeof(tinypy_value_t *);
    size_t index;

    if (__tinypy_preprocessor_value_tick(ctx, 1, 0) == 0) {
        return NULL;
    }
    if (value->type == (uint32_t)TINYPY_BUILD_VALUE_LONG) {
        if (value->long_digit_count > (SIZE_MAX - payload_size) / sizeof(uint16_t)) {
            return NULL;
        }
        payload_size += value->long_digit_count * sizeof(uint16_t);
    }
    else if (value->type == (uint32_t)TINYPY_BUILD_VALUE_STRING || value->type == (uint32_t)TINYPY_BUILD_VALUE_UNICODE) {
        if (value->data_size > SIZE_MAX - payload_size) {
            return NULL;
        }
        payload_size += value->data_size;
    }
    else if (value->type == (uint32_t)TINYPY_BUILD_VALUE_TUPLE) {
        if (value->item_count > (SIZE_MAX - payload_size) / sizeof(tinypy_value_t *)) {
            return NULL;
        }
        payload_size += value->item_count * sizeof(tinypy_value_t *);
    }
    if (__tinypy_preprocessor_bytes_add(ctx, payload_size, 1, 0) == 0) {
        return NULL;
    }
    switch ((tinypy_build_value_type_e)value->type) {
    case TINYPY_BUILD_VALUE_NONE:
        return tinypy_none_get(ctx->vm);
    case TINYPY_BUILD_VALUE_BOOL:
        return tinypy_bool_from_i32(ctx->vm, value->integer_value != 0 ? INT32_C(1) : INT32_C(0));
    case TINYPY_BUILD_VALUE_INTEGER:
        return tinypy_integer_from_i64(ctx->vm, value->integer_value);
    case TINYPY_BUILD_VALUE_LONG:
        return tinypy_long_from_base15_digits(ctx->vm, value->long_sign, value->long_digits, value->long_digit_count);
    case TINYPY_BUILD_VALUE_FLOAT:
        return tinypy_float_from_double(ctx->vm, value->float_value);
    case TINYPY_BUILD_VALUE_STRING:
        return tinypy_string_from_bytes(ctx->vm, value->data, value->data_size);
    case TINYPY_BUILD_VALUE_UNICODE:
        return tinypy_unicode_from_utf8(ctx->vm, (const char *)value->data, value->data_size);
    case TINYPY_BUILD_VALUE_TUPLE: {
        tinypy_value_t **items = NULL;

        if (value->item_count != 0U) {
            items = (tinypy_value_t **)tinypy_internal_compiler_arena_allocate(ctx, value->item_count * sizeof(*items));
            if (items == NULL) {
                return NULL;
            }
        }
        for (index = 0U; index < value->item_count; ++index) {
            items[index] = __tinypy_preprocessor_build_value(ctx, &value->items[index]);
            if (items[index] == NULL) {
                while (index != 0U) {
                    index -= 1U;
                    TINYPY_DECREF(items[index]);
                }
                return NULL;
            }
        }
        result = tinypy_tuple_from_items(ctx->vm, items, value->item_count);
        for (index = 0U; index < value->item_count; ++index) {
            TINYPY_DECREF(items[index]);
        }
        return result;
    }
    default:
        break;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __tinypy_preprocessor_constant_expression(tinypy_compile_ctx_t *ctx, tinypy_ast_expression_t expression, const tinypy_build_value_t *build_value) {
    tinypy_value_t *value = __tinypy_preprocessor_build_value(ctx, build_value);
    tinypy_ast_expression_t replacement;

    if (value == NULL) {
        if (ctx->failed == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "build constant materialization exceeds compiler limits", expression->lineno, expression->col_offset + 1, ctx->out_error);
        }
        return NULL;
    }
    if (tinypy_internal_compiler_arena_add_value(ctx, value) != 0) {
        TINYPY_DECREF(value);
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "build constant reference exceeds compiler arena limit", expression->lineno, expression->col_offset + 1, ctx->out_error);
        return NULL;
    }
    replacement = __tinypy_ast_num(value, expression->lineno, expression->col_offset, ctx);
    return replacement;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_slice_t __tinypy_preprocessor_slice_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_slice_t slice) {
    int index;

    if (slice == NULL) {
        return NULL;
    }
    switch (slice->kind) {
    case TINYPY_AST_KIND_ELLIPSIS:
        break;
    case TINYPY_AST_KIND_SLICE:
        if (slice->v.Slice.lower != NULL) {
            slice->v.Slice.lower = __tinypy_preprocessor_expression_transform(ctx, slice->v.Slice.lower);
        }
        if (slice->v.Slice.upper != NULL) {
            slice->v.Slice.upper = __tinypy_preprocessor_expression_transform(ctx, slice->v.Slice.upper);
        }
        if (slice->v.Slice.step != NULL) {
            slice->v.Slice.step = __tinypy_preprocessor_expression_transform(ctx, slice->v.Slice.step);
        }
        break;
    case TINYPY_AST_KIND_EXT_SLICE:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(slice->v.ExtSlice.dims); ++index) {
            TINYPY_AST_SEQUENCE_SET(slice->v.ExtSlice.dims, index, __tinypy_preprocessor_slice_transform(ctx, (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(slice->v.ExtSlice.dims, index)));
        }
        break;
    case TINYPY_AST_KIND_INDEX:
        slice->v.Index.value = __tinypy_preprocessor_expression_transform(ctx, slice->v.Index.value);
        break;
    default:
        return NULL;
    }
    return ctx->failed == 0 ? slice : NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_preprocessor_expression_sequence_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *sequence) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence) && ctx->failed == 0; ++index) {
        TINYPY_AST_SEQUENCE_SET(sequence, index, __tinypy_preprocessor_expression_transform(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(sequence, index)));
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_preprocessor_arguments_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_arguments_t arguments) {
    __tinypy_preprocessor_expression_sequence_transform(ctx, arguments->defaults);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_preprocessor_comprehensions_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *generators) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(generators) && ctx->failed == 0; ++index) {
        tinypy_ast_comprehension_t generator = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, index);
        generator->iter = __tinypy_preprocessor_expression_transform(ctx, generator->iter);
        __tinypy_preprocessor_expression_sequence_transform(ctx, generator->ifs);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __tinypy_preprocessor_expression_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_expression_t expression) {
    int index;

    if (expression == NULL || ctx->failed != 0) {
        return expression;
    }
    switch (expression->kind) {
    case TINYPY_AST_KIND_NAME:
        if (expression->v.Name.ctx == TINYPY_AST_CONTEXT_LOAD && __tinypy_preprocessor_identifier_reserved(expression->v.Name.id) != 0) {
            const tinypy_build_value_t *value = NULL;
            size_t size;
            const char *name = (const char *)tinypy_string_view(expression->v.Name.id, &size);
            int32_t found = tinypy_build_profile_find(ctx->options.build_profile, name, size, &value);

            assert(found != 0);
            (void)found;
            return __tinypy_preprocessor_constant_expression(ctx, expression, value);
        }
        break;
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
        break;
    case TINYPY_AST_KIND_BOOL_OP:
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.BoolOp.values);
        break;
    case TINYPY_AST_KIND_BIN_OP:
        expression->v.BinOp.left = __tinypy_preprocessor_expression_transform(ctx, expression->v.BinOp.left);
        expression->v.BinOp.right = __tinypy_preprocessor_expression_transform(ctx, expression->v.BinOp.right);
        break;
    case TINYPY_AST_KIND_UNARY_OP:
        expression->v.UnaryOp.operand = __tinypy_preprocessor_expression_transform(ctx, expression->v.UnaryOp.operand);
        break;
    case TINYPY_AST_KIND_LAMBDA:
        __tinypy_preprocessor_arguments_transform(ctx, expression->v.Lambda.args);
        expression->v.Lambda.body = __tinypy_preprocessor_expression_transform(ctx, expression->v.Lambda.body);
        break;
    case TINYPY_AST_KIND_IF_EXP:
        expression->v.IfExp.test = __tinypy_preprocessor_expression_transform(ctx, expression->v.IfExp.test);
        expression->v.IfExp.body = __tinypy_preprocessor_expression_transform(ctx, expression->v.IfExp.body);
        expression->v.IfExp.orelse = __tinypy_preprocessor_expression_transform(ctx, expression->v.IfExp.orelse);
        break;
    case TINYPY_AST_KIND_DICT:
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.Dict.keys);
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.Dict.values);
        break;
    case TINYPY_AST_KIND_SET:
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.Set.elts);
        break;
    case TINYPY_AST_KIND_LIST_COMP:
        expression->v.ListComp.elt = __tinypy_preprocessor_expression_transform(ctx, expression->v.ListComp.elt);
        __tinypy_preprocessor_comprehensions_transform(ctx, expression->v.ListComp.generators);
        break;
    case TINYPY_AST_KIND_SET_COMP:
        expression->v.SetComp.elt = __tinypy_preprocessor_expression_transform(ctx, expression->v.SetComp.elt);
        __tinypy_preprocessor_comprehensions_transform(ctx, expression->v.SetComp.generators);
        break;
    case TINYPY_AST_KIND_DICT_COMP:
        expression->v.DictComp.key = __tinypy_preprocessor_expression_transform(ctx, expression->v.DictComp.key);
        expression->v.DictComp.value = __tinypy_preprocessor_expression_transform(ctx, expression->v.DictComp.value);
        __tinypy_preprocessor_comprehensions_transform(ctx, expression->v.DictComp.generators);
        break;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        expression->v.GeneratorExp.elt = __tinypy_preprocessor_expression_transform(ctx, expression->v.GeneratorExp.elt);
        __tinypy_preprocessor_comprehensions_transform(ctx, expression->v.GeneratorExp.generators);
        break;
    case TINYPY_AST_KIND_YIELD:
        if (expression->v.Yield.value != NULL) {
            expression->v.Yield.value = __tinypy_preprocessor_expression_transform(ctx, expression->v.Yield.value);
        }
        break;
    case TINYPY_AST_KIND_COMPARE:
        expression->v.Compare.left = __tinypy_preprocessor_expression_transform(ctx, expression->v.Compare.left);
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.Compare.comparators);
        break;
    case TINYPY_AST_KIND_CALL:
        expression->v.Call.func = __tinypy_preprocessor_expression_transform(ctx, expression->v.Call.func);
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.Call.args);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords); ++index) {
            tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.keywords, index);
            keyword->value = __tinypy_preprocessor_expression_transform(ctx, keyword->value);
        }
        if (expression->v.Call.starargs != NULL) {
            expression->v.Call.starargs = __tinypy_preprocessor_expression_transform(ctx, expression->v.Call.starargs);
        }
        if (expression->v.Call.kwargs != NULL) {
            expression->v.Call.kwargs = __tinypy_preprocessor_expression_transform(ctx, expression->v.Call.kwargs);
        }
        break;
    case TINYPY_AST_KIND_REPR:
        expression->v.Repr.value = __tinypy_preprocessor_expression_transform(ctx, expression->v.Repr.value);
        break;
    case TINYPY_AST_KIND_ATTRIBUTE:
        expression->v.Attribute.value = __tinypy_preprocessor_expression_transform(ctx, expression->v.Attribute.value);
        break;
    case TINYPY_AST_KIND_SUBSCRIPT:
        expression->v.Subscript.value = __tinypy_preprocessor_expression_transform(ctx, expression->v.Subscript.value);
        expression->v.Subscript.slice = __tinypy_preprocessor_slice_transform(ctx, expression->v.Subscript.slice);
        break;
    case TINYPY_AST_KIND_LIST:
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.List.elts);
        break;
    case TINYPY_AST_KIND_TUPLE:
        __tinypy_preprocessor_expression_sequence_transform(ctx, expression->v.Tuple.elts);
        break;
    default:
        return NULL;
    }
    return ctx->failed == 0 ? expression : NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_identity_literal(tinypy_ast_expression_t expression) {
    if (expression->kind == TINYPY_AST_KIND_NAME) {
        return __tinypy_preprocessor_identifier_equal(expression->v.Name.id, "None", 4U) || __tinypy_preprocessor_identifier_equal(expression->v.Name.id, "True", 4U) || __tinypy_preprocessor_identifier_equal(expression->v.Name.id, "False", 5U);
    }
    if (expression->kind == TINYPY_AST_KIND_NUM) {
        tinypy_value_type_e type = tinypy_typeof(expression->v.Num.n);

        return type == TINYPY_VALUE_NONE || type == TINYPY_VALUE_BOOL;
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_expression_pure(tinypy_ast_expression_t expression) {
    int index;

    switch (expression->kind) {
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
        return 1;
    case TINYPY_AST_KIND_NAME:
        return __tinypy_preprocessor_identifier_equal(expression->v.Name.id, "None", 4U) || __tinypy_preprocessor_identifier_equal(expression->v.Name.id, "True", 4U) || __tinypy_preprocessor_identifier_equal(expression->v.Name.id, "False", 5U);
    case TINYPY_AST_KIND_BOOL_OP:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.BoolOp.values); ++index) {
            if (__tinypy_preprocessor_expression_pure((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.BoolOp.values, index)) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_BIN_OP:
        return __tinypy_preprocessor_expression_pure(expression->v.BinOp.left) && __tinypy_preprocessor_expression_pure(expression->v.BinOp.right);
    case TINYPY_AST_KIND_UNARY_OP:
        return __tinypy_preprocessor_expression_pure(expression->v.UnaryOp.operand);
    case TINYPY_AST_KIND_DICT:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Dict.keys); ++index) {
            if (__tinypy_preprocessor_expression_pure((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Dict.keys, index)) == 0 || __tinypy_preprocessor_expression_pure((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Dict.values, index)) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_SET:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Set.elts); ++index) {
            if (__tinypy_preprocessor_expression_pure((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Set.elts, index)) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_COMPARE:
        if (__tinypy_preprocessor_expression_pure(expression->v.Compare.left) == 0) {
            return 0;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Compare.comparators); ++index) {
            tinypy_ast_expression_t right = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Compare.comparators, index);
            int operation = expression->v.Compare.ops->elements[index];

            if (__tinypy_preprocessor_expression_pure(right) == 0) {
                return 0;
            }
            if ((operation == TINYPY_AST_COMPARE_IS || operation == TINYPY_AST_COMPARE_IS_NOT) && (__tinypy_preprocessor_identity_literal(index == 0 ? expression->v.Compare.left : (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Compare.comparators, index - 1)) == 0 || __tinypy_preprocessor_identity_literal(right) == 0)) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_LIST:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.List.elts); ++index) {
            if (__tinypy_preprocessor_expression_pure((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.List.elts, index)) == 0) {
                return 0;
            }
        }
        return 1;
    case TINYPY_AST_KIND_TUPLE:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Tuple.elts); ++index) {
            if (__tinypy_preprocessor_expression_pure((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Tuple.elts, index)) == 0) {
                return 0;
            }
        }
        return 1;
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_preprocessor_operation_error(tinypy_compile_ctx_t *ctx, tinypy_error_t *error, int32_t line, int32_t column) {
    const char *message = error != NULL ? tinypy_error_message(error, NULL) : "pure expression evaluation failed";

    tinypy_internal_exception_clear_raised(ctx->vm);
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_PREPROCESSOR, message, line, column + 1, ctx->out_error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_value_hashable(const tinypy_value_t *value) {
    tinypy_value_type_e type = tinypy_typeof(value);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    if (type == TINYPY_VALUE_LIST || type == TINYPY_VALUE_DICT || type == TINYPY_VALUE_SET || type == TINYPY_VALUE_BYTEARRAY) {
        return 0;
    }
    if (type != TINYPY_VALUE_TUPLE) {
        return 1;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(value);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(value);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (__tinypy_preprocessor_value_hashable(item) == 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_preprocessor_expression_evaluate(tinypy_compile_ctx_t *ctx, tinypy_ast_expression_t expression, uint32_t future_flags) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *left;
    tinypy_value_t *right;
    tinypy_value_t *result = NULL;
    int index;

    if (__tinypy_preprocessor_tick(ctx, expression->lineno, expression->col_offset) == 0 || __tinypy_preprocessor_value_tick(ctx, expression->lineno, expression->col_offset) == 0) {
        return NULL;
    }
    switch (expression->kind) {
    case TINYPY_AST_KIND_NUM:
        TINYPY_INCREF(expression->v.Num.n);
        return expression->v.Num.n;
    case TINYPY_AST_KIND_STR:
        TINYPY_INCREF(expression->v.Str.s);
        return expression->v.Str.s;
    case TINYPY_AST_KIND_NAME:
        if (__tinypy_preprocessor_identifier_equal(expression->v.Name.id, "None", 4U) != 0) {
            return tinypy_none_get(ctx->vm);
        }
        if (__tinypy_preprocessor_identifier_equal(expression->v.Name.id, "True", 4U) != 0) {
            return tinypy_bool_from_i32(ctx->vm, INT32_C(1));
        }
        return tinypy_bool_from_i32(ctx->vm, INT32_C(0));
    case TINYPY_AST_KIND_UNARY_OP:
        left = __tinypy_preprocessor_expression_evaluate(ctx, expression->v.UnaryOp.operand, future_flags);
        if (left == NULL) {
            return NULL;
        }
        if (expression->v.UnaryOp.op == TINYPY_AST_UNARY_NOT) {
            int32_t truth = tinypy_truth(left, &error);
            TINYPY_DECREF(left);
            if (truth < 0) {
                return __tinypy_preprocessor_operation_error(ctx, error, expression->lineno, expression->col_offset);
            }
            return tinypy_bool_from_i32(ctx->vm, truth == 0 ? INT32_C(1) : INT32_C(0));
        }
        if (expression->v.UnaryOp.op == TINYPY_AST_UNARY_ADD) {
            result = tinypy_positive(left, &error);
        }
        else if (expression->v.UnaryOp.op == TINYPY_AST_UNARY_SUBTRACT) {
            result = tinypy_negative(left, &error);
        }
        else {
            result = tinypy_invert(left, &error);
        }
        TINYPY_DECREF(left);
        if (result == NULL) {
            return __tinypy_preprocessor_operation_error(ctx, error, expression->lineno, expression->col_offset);
        }
        return result;
    case TINYPY_AST_KIND_BOOL_OP:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.BoolOp.values); ++index) {
            int32_t truth;

            result = __tinypy_preprocessor_expression_evaluate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.BoolOp.values, index), future_flags);
            if (result == NULL) {
                return NULL;
            }
            if (index + 1 == TINYPY_AST_SEQUENCE_LENGTH(expression->v.BoolOp.values)) {
                return result;
            }
            truth = tinypy_truth(result, &error);
            if (truth < 0) {
                TINYPY_DECREF(result);
                return __tinypy_preprocessor_operation_error(ctx, error, expression->lineno, expression->col_offset);
            }
            if ((expression->v.BoolOp.op == TINYPY_AST_BOOLEAN_AND && truth == 0) || (expression->v.BoolOp.op == TINYPY_AST_BOOLEAN_OR && truth != 0)) {
                return result;
            }
            TINYPY_DECREF(result);
        }
        break;
    case TINYPY_AST_KIND_BIN_OP:
        left = __tinypy_preprocessor_expression_evaluate(ctx, expression->v.BinOp.left, future_flags);
        if (left == NULL) {
            return NULL;
        }
        right = __tinypy_preprocessor_expression_evaluate(ctx, expression->v.BinOp.right, future_flags);
        if (right == NULL) {
            TINYPY_DECREF(left);
            return NULL;
        }
        switch (expression->v.BinOp.op) {
        case TINYPY_AST_BINARY_ADD:
            result = tinypy_add(left, right, &error);
            break;
        case TINYPY_AST_BINARY_SUBTRACT:
            result = tinypy_subtract(left, right, &error);
            break;
        case TINYPY_AST_BINARY_MULTIPLY:
            result = tinypy_multiply(left, right, &error);
            break;
        case TINYPY_AST_BINARY_DIVIDE:
            result = (future_flags & (uint32_t)TINYPY_COMPILE_FLAG_FUTURE_DIVISION) != 0U ? tinypy_true_divide(left, right, &error) : tinypy_divide(left, right, &error);
            break;
        case TINYPY_AST_BINARY_MODULO:
            result = tinypy_remainder(left, right, &error);
            break;
        case TINYPY_AST_BINARY_POWER:
            result = tinypy_power(left, right, &error);
            break;
        case TINYPY_AST_BINARY_LEFT_SHIFT:
            result = tinypy_left_shift(left, right, &error);
            break;
        case TINYPY_AST_BINARY_RIGHT_SHIFT:
            result = tinypy_right_shift(left, right, &error);
            break;
        case TINYPY_AST_BINARY_BIT_OR:
            result = tinypy_bit_or(left, right, &error);
            break;
        case TINYPY_AST_BINARY_BIT_XOR:
            result = tinypy_bit_xor(left, right, &error);
            break;
        case TINYPY_AST_BINARY_BIT_AND:
            result = tinypy_bit_and(left, right, &error);
            break;
        case TINYPY_AST_BINARY_FLOOR_DIVIDE:
            result = tinypy_floor_divide(left, right, &error);
            break;
        default:
            break;
        }
        TINYPY_DECREF(right);
        TINYPY_DECREF(left);
        if (result == NULL) {
            return __tinypy_preprocessor_operation_error(ctx, error, expression->lineno, expression->col_offset);
        }
        return result;
    case TINYPY_AST_KIND_LIST:
    case TINYPY_AST_KIND_TUPLE: {
        tinypy_ast_sequence_t *elements = expression->kind == TINYPY_AST_KIND_LIST ? expression->v.List.elts : expression->v.Tuple.elts;
        size_t count = (size_t)TINYPY_AST_SEQUENCE_LENGTH(elements);
        tinypy_value_t **items = count != 0U ? (tinypy_value_t **)tinypy_internal_compiler_arena_allocate(ctx, count * sizeof(*items)) : NULL;
        size_t item_index;

        if (count != 0U && items == NULL) {
            return NULL;
        }
        for (item_index = 0U; item_index < count; ++item_index) {
            items[item_index] = __tinypy_preprocessor_expression_evaluate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(elements, (int)item_index), future_flags);
            if (items[item_index] == NULL) {
                while (item_index != 0U) {
                    item_index -= 1U;
                    TINYPY_DECREF(items[item_index]);
                }
                return NULL;
            }
        }
        result = expression->kind == TINYPY_AST_KIND_LIST ? tinypy_list_from_items(ctx->vm, items, count) : tinypy_tuple_from_items(ctx->vm, items, count);
        for (item_index = 0U; item_index < count; ++item_index) {
            TINYPY_DECREF(items[item_index]);
        }
        return result;
    }
    case TINYPY_AST_KIND_DICT:
        result = tinypy_dict_new(ctx->vm);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Dict.keys); ++index) {
            left = __tinypy_preprocessor_expression_evaluate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Dict.keys, index), future_flags);
            right = left != NULL ? __tinypy_preprocessor_expression_evaluate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Dict.values, index), future_flags) : NULL;
            if (right == NULL) {
                if (left != NULL) {
                    TINYPY_DECREF(left);
                }
                TINYPY_DECREF(result);
                return NULL;
            }
            if (__tinypy_preprocessor_value_hashable(left) == 0) {
                TINYPY_DECREF(right);
                TINYPY_DECREF(left);
                TINYPY_DECREF(result);
                tinypy_internal_compiler_error(ctx, TINYPY_ERROR_PREPROCESSOR, "unhashable literal in pure dictionary", expression->lineno, expression->col_offset + 1, ctx->out_error);
                return NULL;
            }
            tinypy_dict_set(result, left, right);
            TINYPY_DECREF(right);
            TINYPY_DECREF(left);
        }
        return result;
    case TINYPY_AST_KIND_SET:
        result = tinypy_set_new(ctx->vm);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Set.elts); ++index) {
            left = __tinypy_preprocessor_expression_evaluate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Set.elts, index), future_flags);
            if (left == NULL) {
                TINYPY_DECREF(result);
                return NULL;
            }
            if (tinypy_set_add(result, left, &error) == 0 && error != NULL) {
                TINYPY_DECREF(left);
                TINYPY_DECREF(result);
                return __tinypy_preprocessor_operation_error(ctx, error, expression->lineno, expression->col_offset);
            }
            TINYPY_DECREF(left);
        }
        return result;
    case TINYPY_AST_KIND_COMPARE:
        left = __tinypy_preprocessor_expression_evaluate(ctx, expression->v.Compare.left, future_flags);
        if (left == NULL) {
            return NULL;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Compare.comparators); ++index) {
            tinypy_compare_operation_e operation;
            int32_t compared;

            right = __tinypy_preprocessor_expression_evaluate(ctx, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Compare.comparators, index), future_flags);
            if (right == NULL) {
                TINYPY_DECREF(left);
                return NULL;
            }
            switch (expression->v.Compare.ops->elements[index]) {
            case TINYPY_AST_COMPARE_EQUAL:
                operation = TINYPY_COMPARE_EQUAL;
                break;
            case TINYPY_AST_COMPARE_NOT_EQUAL:
                operation = TINYPY_COMPARE_NOT_EQUAL;
                break;
            case TINYPY_AST_COMPARE_LESS:
                operation = TINYPY_COMPARE_LESS;
                break;
            case TINYPY_AST_COMPARE_LESS_EQUAL:
                operation = TINYPY_COMPARE_LESS_EQUAL;
                break;
            case TINYPY_AST_COMPARE_GREATER:
                operation = TINYPY_COMPARE_GREATER;
                break;
            case TINYPY_AST_COMPARE_GREATER_EQUAL:
                operation = TINYPY_COMPARE_GREATER_EQUAL;
                break;
            case TINYPY_AST_COMPARE_IS:
                operation = TINYPY_COMPARE_IS;
                break;
            case TINYPY_AST_COMPARE_IS_NOT:
                operation = TINYPY_COMPARE_IS_NOT;
                break;
            case TINYPY_AST_COMPARE_IN:
                operation = TINYPY_COMPARE_IN;
                break;
            default:
                operation = TINYPY_COMPARE_NOT_IN;
                break;
            }
            compared = tinypy_compare_bool(left, right, operation, &error);
            TINYPY_DECREF(left);
            if (error != NULL) {
                TINYPY_DECREF(right);
                return __tinypy_preprocessor_operation_error(ctx, error, expression->lineno, expression->col_offset);
            }
            if (compared == 0) {
                TINYPY_DECREF(right);
                return tinypy_bool_from_i32(ctx->vm, INT32_C(0));
            }
            left = right;
        }
        TINYPY_DECREF(left);
        return tinypy_bool_from_i32(ctx->vm, INT32_C(1));
    default:
        break;
    }
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_PREPROCESSOR, "unsupported pure expression", expression->lineno, expression->col_offset + 1, ctx->out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_builder_append(tinypy_compile_ctx_t *ctx, tinypy_preprocessor_sequence_builder_t *builder, tinypy_ast_statement_t statement) {
    tinypy_preprocessor_sequence_node_t *node = (tinypy_preprocessor_sequence_node_t *)tinypy_internal_compiler_arena_allocate(ctx, sizeof(*node));

    if (node == NULL) {
        return 0;
    }
    node->statement = statement;
    if (builder->tail != NULL) {
        builder->tail->next = node;
    }
    else {
        builder->head = node;
    }
    builder->tail = node;
    builder->size += 1U;
    return 1;
}

static int32_t __tinypy_preprocessor_sequence_append_transformed(tinypy_compile_ctx_t *ctx, tinypy_preprocessor_sequence_builder_t *builder, tinypy_ast_sequence_t *sequence, uint32_t future_flags);

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_statement_append_transformed(tinypy_compile_ctx_t *ctx, tinypy_preprocessor_sequence_builder_t *builder, tinypy_ast_statement_t statement, uint32_t future_flags) {
    int index;

    switch (statement->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        __tinypy_preprocessor_arguments_transform(ctx, statement->v.FunctionDef.args);
        __tinypy_preprocessor_expression_sequence_transform(ctx, statement->v.FunctionDef.decorator_list);
        statement->v.FunctionDef.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.FunctionDef.body);
        break;
    case TINYPY_AST_KIND_CLASS_DEF:
        __tinypy_preprocessor_expression_sequence_transform(ctx, statement->v.ClassDef.bases);
        __tinypy_preprocessor_expression_sequence_transform(ctx, statement->v.ClassDef.decorator_list);
        statement->v.ClassDef.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.ClassDef.body);
        break;
    case TINYPY_AST_KIND_RETURN:
        if (statement->v.Return.value != NULL) {
            statement->v.Return.value = __tinypy_preprocessor_expression_transform(ctx, statement->v.Return.value);
        }
        break;
    case TINYPY_AST_KIND_DELETE:
        __tinypy_preprocessor_expression_sequence_transform(ctx, statement->v.Delete.targets);
        break;
    case TINYPY_AST_KIND_ASSIGN:
        __tinypy_preprocessor_expression_sequence_transform(ctx, statement->v.Assign.targets);
        statement->v.Assign.value = __tinypy_preprocessor_expression_transform(ctx, statement->v.Assign.value);
        break;
    case TINYPY_AST_KIND_AUG_ASSIGN:
        statement->v.AugAssign.value = __tinypy_preprocessor_expression_transform(ctx, statement->v.AugAssign.value);
        break;
    case TINYPY_AST_KIND_PRINT:
        if (statement->v.Print.dest != NULL) {
            statement->v.Print.dest = __tinypy_preprocessor_expression_transform(ctx, statement->v.Print.dest);
        }
        __tinypy_preprocessor_expression_sequence_transform(ctx, statement->v.Print.values);
        break;
    case TINYPY_AST_KIND_FOR:
        statement->v.For.iter = __tinypy_preprocessor_expression_transform(ctx, statement->v.For.iter);
        statement->v.For.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.For.body);
        statement->v.For.orelse = __tinypy_preprocessor_sequence_transform(ctx, statement->v.For.orelse);
        break;
    case TINYPY_AST_KIND_WHILE:
        statement->v.While.test = __tinypy_preprocessor_expression_transform(ctx, statement->v.While.test);
        statement->v.While.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.While.body);
        statement->v.While.orelse = __tinypy_preprocessor_sequence_transform(ctx, statement->v.While.orelse);
        break;
    case TINYPY_AST_KIND_IF: {
        tinypy_value_t *value;
        int32_t truth;
        tinypy_error_t *error = NULL;

        statement->v.If.test = __tinypy_preprocessor_expression_transform(ctx, statement->v.If.test);
        if (ctx->failed != 0) {
            return 0;
        }
        if (__tinypy_preprocessor_expression_pure(statement->v.If.test) != 0) {
            value = __tinypy_preprocessor_expression_evaluate(ctx, statement->v.If.test, future_flags);
            if (value == NULL) {
                return 0;
            }
            truth = tinypy_truth(value, &error);
            TINYPY_DECREF(value);
            if (truth < 0) {
                (void)__tinypy_preprocessor_operation_error(ctx, error, statement->lineno, statement->col_offset);
                return 0;
            }
            return __tinypy_preprocessor_sequence_append_transformed(ctx, builder, truth != 0 ? statement->v.If.body : statement->v.If.orelse, future_flags);
        }
        statement->v.If.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.If.body);
        statement->v.If.orelse = __tinypy_preprocessor_sequence_transform(ctx, statement->v.If.orelse);
        break;
    }
    case TINYPY_AST_KIND_WITH:
        statement->v.With.context_expr = __tinypy_preprocessor_expression_transform(ctx, statement->v.With.context_expr);
        statement->v.With.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.With.body);
        break;
    case TINYPY_AST_KIND_RAISE:
        if (statement->v.Raise.type != NULL) {
            statement->v.Raise.type = __tinypy_preprocessor_expression_transform(ctx, statement->v.Raise.type);
        }
        if (statement->v.Raise.inst != NULL) {
            statement->v.Raise.inst = __tinypy_preprocessor_expression_transform(ctx, statement->v.Raise.inst);
        }
        if (statement->v.Raise.tback != NULL) {
            statement->v.Raise.tback = __tinypy_preprocessor_expression_transform(ctx, statement->v.Raise.tback);
        }
        break;
    case TINYPY_AST_KIND_TRY_EXCEPT:
        statement->v.TryExcept.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.TryExcept.body);
        statement->v.TryExcept.orelse = __tinypy_preprocessor_sequence_transform(ctx, statement->v.TryExcept.orelse);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.handlers); ++index) {
            tinypy_ast_exception_handler_t handler = (tinypy_ast_exception_handler_t)TINYPY_AST_SEQUENCE_GET(statement->v.TryExcept.handlers, index);
            if (handler->v.ExceptHandler.type != NULL) {
                handler->v.ExceptHandler.type = __tinypy_preprocessor_expression_transform(ctx, handler->v.ExceptHandler.type);
            }
            handler->v.ExceptHandler.body = __tinypy_preprocessor_sequence_transform(ctx, handler->v.ExceptHandler.body);
        }
        break;
    case TINYPY_AST_KIND_TRY_FINALLY:
        statement->v.TryFinally.body = __tinypy_preprocessor_sequence_transform(ctx, statement->v.TryFinally.body);
        statement->v.TryFinally.finalbody = __tinypy_preprocessor_sequence_transform(ctx, statement->v.TryFinally.finalbody);
        break;
    case TINYPY_AST_KIND_ASSERT:
        statement->v.Assert.test = __tinypy_preprocessor_expression_transform(ctx, statement->v.Assert.test);
        if (statement->v.Assert.msg != NULL) {
            statement->v.Assert.msg = __tinypy_preprocessor_expression_transform(ctx, statement->v.Assert.msg);
        }
        break;
    case TINYPY_AST_KIND_EXEC:
        statement->v.Exec.body = __tinypy_preprocessor_expression_transform(ctx, statement->v.Exec.body);
        if (statement->v.Exec.globals != NULL) {
            statement->v.Exec.globals = __tinypy_preprocessor_expression_transform(ctx, statement->v.Exec.globals);
        }
        if (statement->v.Exec.locals != NULL) {
            statement->v.Exec.locals = __tinypy_preprocessor_expression_transform(ctx, statement->v.Exec.locals);
        }
        break;
    case TINYPY_AST_KIND_EXPR:
        statement->v.Expr.value = __tinypy_preprocessor_expression_transform(ctx, statement->v.Expr.value);
        break;
    case TINYPY_AST_KIND_IMPORT:
    case TINYPY_AST_KIND_IMPORT_FROM:
    case TINYPY_AST_KIND_GLOBAL:
    case TINYPY_AST_KIND_PASS:
    case TINYPY_AST_KIND_BREAK:
    case TINYPY_AST_KIND_CONTINUE:
        break;
    default:
        return 0;
    }
    if (ctx->failed != 0) {
        return 0;
    }
    return __tinypy_preprocessor_builder_append(ctx, builder, statement);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_preprocessor_sequence_append_transformed(tinypy_compile_ctx_t *ctx, tinypy_preprocessor_sequence_builder_t *builder, tinypy_ast_sequence_t *sequence, uint32_t future_flags) {
    int index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (__tinypy_preprocessor_statement_append_transformed(ctx, builder, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(sequence, index), future_flags) == 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_preprocessor_sequence_transform_with_flags(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *sequence, uint32_t future_flags) {
    tinypy_preprocessor_sequence_builder_t builder;
    tinypy_preprocessor_sequence_node_t *node;
    tinypy_ast_sequence_t *result;
    size_t index = 0U;

    (void)memset(&builder, 0, sizeof(builder));
    if (__tinypy_preprocessor_sequence_append_transformed(ctx, &builder, sequence, future_flags) == 0) {
        return NULL;
    }
    if (builder.size > (size_t)INT_MAX) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "preprocessed suite is too large", 1, 1, ctx->out_error);
        return NULL;
    }
    if (builder.size == 0U) {
        tinypy_ast_statement_t pass_statement = __tinypy_ast_pass(1, 0, ctx);

        if (pass_statement == NULL || __tinypy_preprocessor_builder_append(ctx, &builder, pass_statement) == 0) {
            return NULL;
        }
    }
    result = TINYPY_AST_SEQUENCE_NEW((int)builder.size, ctx);
    if (result == NULL) {
        return NULL;
    }
    node = builder.head;
    while (node != NULL) {
        TINYPY_AST_SEQUENCE_SET(result, (int)index, node->statement);
        index += 1U;
        node = node->next;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_preprocessor_sequence_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_sequence_t *sequence) {
    return __tinypy_preprocessor_sequence_transform_with_flags(ctx, sequence, ctx->preprocessor_future_flags);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_preprocessor_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_module_t module, uint32_t future_flags) {
    assert(ctx != NULL);
    assert(module != NULL);
    assert(ctx->options.build_profile != NULL);
    ctx->preprocessor_future_flags = future_flags;
    switch (module->kind) {
    case TINYPY_AST_KIND_MODULE:
        if (__tinypy_preprocessor_statement_sequence_validate(ctx, module->v.Module.body) == 0) {
            return 0;
        }
        module->v.Module.body = __tinypy_preprocessor_sequence_transform_with_flags(ctx, module->v.Module.body, future_flags);
        break;
    case TINYPY_AST_KIND_INTERACTIVE:
        if (__tinypy_preprocessor_statement_sequence_validate(ctx, module->v.Interactive.body) == 0) {
            return 0;
        }
        module->v.Interactive.body = __tinypy_preprocessor_sequence_transform_with_flags(ctx, module->v.Interactive.body, future_flags);
        break;
    case TINYPY_AST_KIND_EXPRESSION:
        if (__tinypy_preprocessor_expression_validate(ctx, module->v.Expression.body) == 0) {
            return 0;
        }
        module->v.Expression.body = __tinypy_preprocessor_expression_transform(ctx, module->v.Expression.body);
        break;
    case TINYPY_AST_KIND_SUITE:
        if (__tinypy_preprocessor_statement_sequence_validate(ctx, module->v.Suite.body) == 0) {
            return 0;
        }
        module->v.Suite.body = __tinypy_preprocessor_sequence_transform_with_flags(ctx, module->v.Suite.body, future_flags);
        break;
    default:
        return 0;
    }
    ctx->preprocessor_future_flags = 0U;
    return ctx->failed == 0;
}
