#include "internal.h"

#include "ast_nodes.h"
#include "value_ops.h"

#include "tinypy/comparison.h"
#include "tinypy/list.h"
#include "tinypy/operator.h"
#include "tinypy/tuple.h"
#include "tinypy/value.h"

#include <limits.h>
#include <string.h>

typedef struct tinypy_meta_binding_t {
    struct tinypy_meta_binding_t *next;
    tinypy_ast_identifier_t name;
    tinypy_value_t *value;
} tinypy_meta_binding_t;

typedef struct tinypy_meta_template_t {
    struct tinypy_meta_template_t *next;
    tinypy_ast_identifier_t name;
    tinypy_ast_statement_t definition;
} tinypy_meta_template_t;

typedef struct tinypy_meta_statement_node_t {
    struct tinypy_meta_statement_node_t *next;
    tinypy_ast_statement_t statement;
    int32_t template_line;
    int32_t template_column;
} tinypy_meta_statement_node_t;

typedef struct tinypy_meta_statement_builder_t {
    tinypy_meta_statement_node_t *head;
    tinypy_meta_statement_node_t *tail;
    size_t size;
} tinypy_meta_statement_builder_t;

typedef struct tinypy_meta_context_t {
    tinypy_compile_ctx_t *compile;
    tinypy_meta_template_t *templates;
    tinypy_meta_binding_t *bindings;
    tinypy_ast_identifier_t current_class;
    size_t expansion_depth;
    int32_t expansion_line;
    int32_t expansion_column;
} tinypy_meta_context_t;

static tinypy_ast_expression_t __tinypy_meta_clone_expression(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression);
static tinypy_ast_statement_t __tinypy_meta_clone_statement(tinypy_meta_context_t *meta, tinypy_ast_statement_t statement);
static tinypy_ast_sequence_t *__tinypy_meta_expand_sequence(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *sequence);
static tinypy_bool_t __tinypy_meta_runtime_expression_validate(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression);
static tinypy_bool_t __tinypy_meta_runtime_statement_validate(tinypy_meta_context_t *meta, tinypy_ast_statement_t statement);

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_identifier_equal(tinypy_ast_identifier_t identifier, const char *name, size_t name_size) {
    const char *data;
    size_t size;

    if (identifier == NULL) {
        return TINYPY_FALSE;
    }
    data = (const char *)tinypy_string_view(identifier, &size);
    tinypy_bool_t return_value_1 = size == name_size && (size == 0U || memcmp(data, name, size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_name_expression(tinypy_ast_expression_t expression, const char *name, size_t name_size) {
    tinypy_bool_t return_value_1 = expression != NULL && expression->kind == TINYPY_AST_KIND_NAME && expression->v.Name.ctx == TINYPY_AST_CONTEXT_LOAD && __tinypy_meta_identifier_equal(expression->v.Name.id, name, name_size) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_identifier_is_builtin(tinypy_ast_identifier_t identifier) {
    tinypy_bool_t return_value_1 = __tinypy_meta_identifier_equal(identifier, "meta", 4U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_attribute_expression(tinypy_ast_expression_t expression, const char *attribute, size_t attribute_size) {
    tinypy_bool_t return_value_1 = expression != NULL && expression->kind == TINYPY_AST_KIND_ATTRIBUTE && expression->v.Attribute.ctx == TINYPY_AST_CONTEXT_LOAD && __tinypy_meta_name_expression(expression->v.Attribute.value, "meta", 4U) != 0 && __tinypy_meta_identifier_equal(expression->v.Attribute.attr, attribute, attribute_size) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_call_expression(tinypy_ast_expression_t expression, const char *attribute, size_t attribute_size) {
    tinypy_bool_t return_value_1 = expression != NULL && expression->kind == TINYPY_AST_KIND_CALL && __tinypy_meta_attribute_expression(expression->v.Call.func, attribute, attribute_size) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_fail(tinypy_meta_context_t *meta, const char *message, int32_t line, int32_t column) {
    tinypy_internal_compiler_error(meta->compile, TINYPY_ERROR_META, message, line, column + 1, meta->compile->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_limit(tinypy_meta_context_t *meta, const char *message, int32_t line, int32_t column) {
    tinypy_internal_compiler_error(meta->compile, TINYPY_ERROR_COMPILER_LIMIT, message, line, column + 1, meta->compile->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_tick(tinypy_meta_context_t *meta, int32_t line, int32_t column) {
    tinypy_compile_ctx_t *ctx = meta->compile;

    if (ctx->limits.max_preprocessor_operations != 0U && ctx->preprocessor_operations >= ctx->limits.max_preprocessor_operations) {
        tinypy_bool_t return_value_1 = __tinypy_meta_limit(meta, "meta operation limit exceeded", line, column);
        return return_value_1;
    }
    ctx->preprocessor_operations += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_generated_node(tinypy_meta_context_t *meta, int32_t line, int32_t column) {
    if (meta->compile->limits.max_generated_ast_nodes != 0U && meta->compile->generated_ast_nodes >= meta->compile->limits.max_generated_ast_nodes) {
        tinypy_bool_t return_value_1 = __tinypy_meta_limit(meta, "generated AST node limit exceeded", line, column);
        return return_value_1;
    }
    meta->compile->generated_ast_nodes += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_meta_generated_line(const tinypy_meta_context_t *meta, int32_t source_line) {
    return meta->expansion_line > 0 ? meta->expansion_line : source_line;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_meta_generated_column(const tinypy_meta_context_t *meta, int32_t source_column) {
    return meta->expansion_line > 0 ? meta->expansion_column : source_column;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_meta_track(tinypy_meta_context_t *meta, tinypy_value_t *value, int32_t line, int32_t column) {
    size_t bytes = sizeof(tinypy_value_t *);

    if (value == NULL) {
        return NULL;
    }
    if (meta->compile->limits.max_preprocessor_value_nodes != 0U && meta->compile->preprocessor_value_nodes >= meta->compile->limits.max_preprocessor_value_nodes) {
        TINYPY_DECREF(value);
        (void)__tinypy_meta_limit(meta, "meta value limit exceeded", line, column);
        return NULL;
    }
    meta->compile->preprocessor_value_nodes += 1U;
    if (tinypy_typeof(value) == TINYPY_VALUE_STRING) {
        size_t size;
        (void)tinypy_string_view(value, &size);
        bytes += size;
    }
    else if (tinypy_typeof(value) == TINYPY_VALUE_UNICODE) {
        size_t size;
        size_t code_points;
        (void)tinypy_unicode_utf8_view(value, &size, &code_points);
        bytes += size;
    }
    else if (tinypy_typeof(value) == TINYPY_VALUE_TUPLE) {
        bytes += TINYPY_TUPLE_SIZE(value) * sizeof(tinypy_value_t *);
    }
    else if (tinypy_typeof(value) == TINYPY_VALUE_LIST) {
        bytes += TINYPY_LIST_SIZE(value) * sizeof(tinypy_value_t *);
    }
    if (meta->compile->limits.max_preprocessor_bytes != 0U && (meta->compile->preprocessor_bytes > meta->compile->limits.max_preprocessor_bytes || bytes > meta->compile->limits.max_preprocessor_bytes - meta->compile->preprocessor_bytes)) {
        TINYPY_DECREF(value);
        (void)__tinypy_meta_limit(meta, "meta byte limit exceeded", line, column);
        return NULL;
    }
    meta->compile->preprocessor_bytes += bytes;
    if (tinypy_internal_compiler_arena_add_value(meta->compile, value) != 0) {
        TINYPY_DECREF(value);
        (void)__tinypy_meta_limit(meta, "meta value exceeds compiler arena limit", line, column);
        return NULL;
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_meta_binding_find(tinypy_meta_context_t *meta, tinypy_ast_identifier_t name) {
    tinypy_meta_binding_t *binding = meta->bindings;

    while (binding != NULL) {
        size_t left_size;
        size_t right_size;
        const void *left = tinypy_string_view(binding->name, &left_size);
        const void *right = tinypy_string_view(name, &right_size);

        if (left_size == right_size && (left_size == 0U || memcmp(left, right, left_size) == 0)) {
            return binding->value;
        }
        binding = binding->next;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_binding_set(tinypy_meta_context_t *meta, tinypy_ast_identifier_t name, tinypy_value_t *value) {
    tinypy_meta_binding_t *binding = meta->bindings;

    while (binding != NULL) {
        size_t left_size;
        size_t right_size;
        const void *left = tinypy_string_view(binding->name, &left_size);
        const void *right = tinypy_string_view(name, &right_size);

        if (left_size == right_size && (left_size == 0U || memcmp(left, right, left_size) == 0)) {
            binding->value = value;
            return TINYPY_TRUE;
        }
        binding = binding->next;
    }
    binding = (tinypy_meta_binding_t *)tinypy_internal_compiler_arena_allocate(meta->compile, sizeof(*binding));
    if (binding == NULL) {
        return TINYPY_FALSE;
    }
    binding->name = name;
    binding->value = value;
    binding->next = meta->bindings;
    meta->bindings = binding;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_identifier_t __tinypy_meta_identifier_from_value(tinypy_meta_context_t *meta, tinypy_value_t *value, int32_t line, int32_t column) {
    const char *bytes;
    size_t size;
    size_t index;

    if (tinypy_typeof(value) == TINYPY_VALUE_STRING) {
        bytes = (const char *)tinypy_string_view(value, &size);
    }
    else if (tinypy_typeof(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;
        bytes = tinypy_unicode_utf8_view(value, &size, &code_points);
    }
    else {
        (void)__tinypy_meta_fail(meta, "generated name must be str or unicode", line, column);
        return NULL;
    }
    if (size == 0U || !((bytes[0] >= 'A' && bytes[0] <= 'Z') || (bytes[0] >= 'a' && bytes[0] <= 'z') || bytes[0] == '_')) {
        (void)__tinypy_meta_fail(meta, "generated name is not a valid Python identifier", line, column);
        return NULL;
    }
    for (index = 1U; index < size; ++index) {
        if (!((bytes[index] >= 'A' && bytes[index] <= 'Z') || (bytes[index] >= 'a' && bytes[index] <= 'z') || (bytes[index] >= '0' && bytes[index] <= '9') || bytes[index] == '_')) {
            (void)__tinypy_meta_fail(meta, "generated name is not a valid ASCII Python identifier", line, column);
            return NULL;
        }
    }
    tinypy_value_t *identifier = tinypy_string_from_bytes(meta->compile->vm, bytes, size);
    tinypy_ast_identifier_t return_value_1 = (tinypy_ast_identifier_t)__tinypy_meta_track(meta, identifier, line, column);
    return return_value_1;
}

static tinypy_value_t *__tinypy_meta_eval(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression);

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_eval_integer(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression, int64_t *out_value) {
    tinypy_value_t *value = __tinypy_meta_eval(meta, expression);

    if (value == NULL) {
        return TINYPY_FALSE;
    }
    if (tinypy_typeof(value) != TINYPY_VALUE_INTEGER) {
        tinypy_bool_t return_value_1 = __tinypy_meta_fail(meta, "meta.range arguments must be integers", expression->lineno, expression->col_offset);
        return return_value_1;
    }
    *out_value = tinypy_integer_as_i64(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_meta_eval_error(tinypy_meta_context_t *meta, tinypy_error_t *error, tinypy_ast_expression_t expression) {
    const char *message = error != NULL ? tinypy_error_message(error, NULL) : "meta expression evaluation failed";

    tinypy_internal_exception_clear_raised(meta->compile->vm);
    (void)__tinypy_meta_fail(meta, message, expression->lineno, expression->col_offset);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_meta_eval_call(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression) {
    tinypy_vm_t *vm = meta->compile->vm;
    int32_t index;

    if (TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords) != 0 || expression->v.Call.starargs != NULL || expression->v.Call.kwargs != NULL) {
        (void)__tinypy_meta_fail(meta, "meta intrinsic does not accept keyword or unpacked arguments", expression->lineno, expression->col_offset);
        return NULL;
    }
    if (__tinypy_meta_attribute_expression(expression->v.Call.func, "concat", 6U) != 0) {
        tinypy_value_t *string_from_bytes = tinypy_string_from_bytes(vm, NULL, 0U);
        tinypy_value_t *result = __tinypy_meta_track(meta, string_from_bytes, expression->lineno, expression->col_offset);

        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.args); ++index) {
            tinypy_value_t *item = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, index));
            tinypy_error_t *error = NULL;
            tinypy_value_t *joined;

            if (item == NULL) {
                return NULL;
            }
            if (tinypy_typeof(item) != TINYPY_VALUE_STRING && tinypy_typeof(item) != TINYPY_VALUE_UNICODE) {
                (void)__tinypy_meta_fail(meta, "meta.concat arguments must be str or unicode", expression->lineno, expression->col_offset);
                return NULL;
            }
            joined = tinypy_add(result, item, &error);
            if (joined == NULL) {
                tinypy_value_t *return_value_1 = __tinypy_meta_eval_error(meta, error, expression);
                return return_value_1;
            }
            result = __tinypy_meta_track(meta, joined, expression->lineno, expression->col_offset);
            if (result == NULL) {
                return NULL;
            }
        }
        return result;
    }
    if (__tinypy_meta_attribute_expression(expression->v.Call.func, "range", 5U) != 0) {
        int64_t start = 0;
        int64_t stop;
        int64_t step = 1;
        int32_t count = TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.args);
        tinypy_value_t *list;
        int64_t current;

        if (count < 1 || count > 3) {
            (void)__tinypy_meta_fail(meta, "meta.range expects one to three integer arguments", expression->lineno, expression->col_offset);
            return NULL;
        }
        if (count == 1) {
            if (__tinypy_meta_eval_integer(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, 0), &stop) == 0) {
                return NULL;
            }
        }
        else {
            if (__tinypy_meta_eval_integer(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, 0), &start) == 0 || __tinypy_meta_eval_integer(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, 1), &stop) == 0) {
                return NULL;
            }
            if (count == 3 && __tinypy_meta_eval_integer(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, 2), &step) == 0) {
                return NULL;
            }
        }
        if (step == 0) {
            (void)__tinypy_meta_fail(meta, "meta.range step cannot be zero", expression->lineno, expression->col_offset);
            return NULL;
        }
        tinypy_value_t *list_from_items = tinypy_list_from_items(vm, NULL, 0U);
        list = __tinypy_meta_track(meta, list_from_items, expression->lineno, expression->col_offset);
        for (current = start; (step > 0 && current < stop) || (step < 0 && current > stop); current += step) {
            tinypy_value_t *item;

            if (__tinypy_meta_tick(meta, expression->lineno, expression->col_offset) == 0) {
                return NULL;
            }
            tinypy_value_t *integer_from_i64 = tinypy_integer_from_i64(vm, current);
            item = __tinypy_meta_track(meta, integer_from_i64, expression->lineno, expression->col_offset);
            if (item == NULL) {
                return NULL;
            }
            tinypy_list_append(list, item);
            if ((step > 0 && current > INT64_MAX - step) || (step < 0 && current < INT64_MIN - step)) {
                break;
            }
        }
        return list;
    }
    (void)__tinypy_meta_fail(meta, "unsupported meta intrinsic", expression->lineno, expression->col_offset);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_meta_eval(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression) {
    tinypy_value_t * function_result;
    tinypy_vm_t *vm = meta->compile->vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *left;
    tinypy_value_t *right;
    tinypy_value_t *result = NULL;
    int32_t index;

    if (__tinypy_meta_tick(meta, expression->lineno, expression->col_offset) == 0) {
        return NULL;
    }
    switch (expression->kind) {
    case TINYPY_AST_KIND_NUM:
        return expression->v.Num.n;
    case TINYPY_AST_KIND_STR:
        return expression->v.Str.s;
    case TINYPY_AST_KIND_NAME:
        result = __tinypy_meta_binding_find(meta, expression->v.Name.id);
        if (result != NULL) {
            return result;
        }
        if (__tinypy_meta_identifier_equal(expression->v.Name.id, "None", 4U) != 0) {
            tinypy_value_t *none = tinypy_none_get(vm);
            tinypy_value_t *return_value_1 = __tinypy_meta_track(meta, none, expression->lineno, expression->col_offset);
            return return_value_1;
        }
        if (__tinypy_meta_identifier_equal(expression->v.Name.id, "True", 4U) != 0) {
            tinypy_value_t *bool_from_i32_2 = tinypy_bool_from_i32(vm, INT32_C(1));
            tinypy_value_t *return_value_2 = __tinypy_meta_track(meta, bool_from_i32_2, expression->lineno, expression->col_offset);
            return return_value_2;
        }
        if (__tinypy_meta_identifier_equal(expression->v.Name.id, "False", 5U) != 0) {
            tinypy_value_t *bool_from_i32_2 = tinypy_bool_from_i32(vm, INT32_C(0));
            tinypy_value_t *return_value_3 = __tinypy_meta_track(meta, bool_from_i32_2, expression->lineno, expression->col_offset);
            return return_value_3;
        }
        (void)__tinypy_meta_fail(meta, "meta expression references a runtime name", expression->lineno, expression->col_offset);
        return NULL;
    case TINYPY_AST_KIND_CALL:
        function_result = __tinypy_meta_eval_call(meta, expression);
        return function_result;
    case TINYPY_AST_KIND_TUPLE:
    case TINYPY_AST_KIND_LIST: {
        tinypy_ast_sequence_t *elements = expression->kind == TINYPY_AST_KIND_TUPLE ? expression->v.Tuple.elts : expression->v.List.elts;
        size_t count = (size_t)TINYPY_AST_SEQUENCE_LENGTH(elements);
        tinypy_value_t **items = count != 0U ? (tinypy_value_t **)tinypy_internal_compiler_arena_allocate(meta->compile, count * sizeof(*items)) : NULL;
        size_t item_index;

        if (count != 0U && items == NULL) {
            return NULL;
        }
        for (item_index = 0U; item_index < count; ++item_index) {
            items[item_index] = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(elements, (int32_t)item_index));
            if (items[item_index] == NULL) {
                return NULL;
            }
        }
        result = expression->kind == TINYPY_AST_KIND_TUPLE ? tinypy_tuple_from_items(vm, items, count) : tinypy_list_from_items(vm, items, count);
        tinypy_value_t *return_value_4 = __tinypy_meta_track(meta, result, expression->lineno, expression->col_offset);
        return return_value_4;
    }
    case TINYPY_AST_KIND_IF_EXP: {
        tinypy_ast_expression_t selected_expression;
        int32_t truth;

        left = __tinypy_meta_eval(meta, expression->v.IfExp.test);
        if (left == NULL) {
            return NULL;
        }
        truth = tinypy_truth(left, &error);
        selected_expression = truth != 0 ? expression->v.IfExp.body : expression->v.IfExp.orelse;
        tinypy_value_t *return_value_5 = __tinypy_meta_eval(meta, selected_expression);
        return return_value_5;
    }
    case TINYPY_AST_KIND_UNARY_OP:
        left = __tinypy_meta_eval(meta, expression->v.UnaryOp.operand);
        if (left == NULL) {
            return NULL;
        }
        if (expression->v.UnaryOp.op == TINYPY_AST_UNARY_NOT) {
            int32_t truth = tinypy_truth(left, &error);
            tinypy_value_t *bool_from_i32_3 = tinypy_bool_from_i32(vm, truth == 0);
            tinypy_value_t *return_value_6 = __tinypy_meta_track(meta, bool_from_i32_3, expression->lineno, expression->col_offset);
            return return_value_6;
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
        tinypy_value_t *return_value_7 = result != NULL ? __tinypy_meta_track(meta, result, expression->lineno, expression->col_offset) : __tinypy_meta_eval_error(meta, error, expression);
        return return_value_7;
    case TINYPY_AST_KIND_BIN_OP:
        left = __tinypy_meta_eval(meta, expression->v.BinOp.left);
        right = left != NULL ? __tinypy_meta_eval(meta, expression->v.BinOp.right) : NULL;
        if (right == NULL) {
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
            result = tinypy_divide(left, right, &error);
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
        tinypy_value_t *return_value_8 = result != NULL ? __tinypy_meta_track(meta, result, expression->lineno, expression->col_offset) : __tinypy_meta_eval_error(meta, error, expression);
        return return_value_8;
    case TINYPY_AST_KIND_BOOL_OP:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.BoolOp.values); ++index) {
            result = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.BoolOp.values, index));
            if (result == NULL) {
                return NULL;
            }
            if (index + 1 == TINYPY_AST_SEQUENCE_LENGTH(expression->v.BoolOp.values)) {
                return result;
            }
            if ((expression->v.BoolOp.op == TINYPY_AST_BOOLEAN_AND && tinypy_truth(result, &error) == 0) || (expression->v.BoolOp.op == TINYPY_AST_BOOLEAN_OR && tinypy_truth(result, &error) != 0)) {
                return result;
            }
        }
        break;
    case TINYPY_AST_KIND_COMPARE:
        left = __tinypy_meta_eval(meta, expression->v.Compare.left);
        if (left == NULL) {
            return NULL;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Compare.comparators); ++index) {
            tinypy_compare_operation_e operation;
            int32_t compared;

            right = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Compare.comparators, index));
            if (right == NULL) {
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
            if (error != NULL) {
                tinypy_value_t *return_value_9 = __tinypy_meta_eval_error(meta, error, expression);
                return return_value_9;
            }
            if (compared == 0) {
                tinypy_value_t *bool_from_i32_2 = tinypy_bool_from_i32(vm, INT32_C(0));
                tinypy_value_t *return_value_10 = __tinypy_meta_track(meta, bool_from_i32_2, expression->lineno, expression->col_offset);
                return return_value_10;
            }
            left = right;
        }
        tinypy_value_t *bool_from_i32 = tinypy_bool_from_i32(vm, INT32_C(1));
        tinypy_value_t *return_value_11 = __tinypy_meta_track(meta, bool_from_i32, expression->lineno, expression->col_offset);
        return return_value_11;
    default:
        break;
    }
    (void)__tinypy_meta_fail(meta, "expression is not allowed during meta expansion", expression->lineno, expression->col_offset);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_meta_clone_expression_sequence(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *source) {
    int32_t index;

    if (source == NULL) {
        return NULL;
    }
    tinypy_ast_sequence_t *result = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(source), meta->compile);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source); ++index) {
        TINYPY_AST_SEQUENCE_SET(result, index, __tinypy_meta_clone_expression(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(source, index)));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_integer_sequence_t *__tinypy_meta_clone_integer_sequence(tinypy_meta_context_t *meta, tinypy_ast_integer_sequence_t *source) {
    int32_t index;

    if (source == NULL) {
        return NULL;
    }
    tinypy_ast_integer_sequence_t *result = TINYPY_AST_INTEGER_SEQUENCE_NEW(source->size, meta->compile);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < source->size; ++index) {
        result->elements[index] = source->elements[index];
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_slice_t __tinypy_meta_clone_slice(tinypy_meta_context_t *meta, tinypy_ast_slice_t source) {
    tinypy_ast_slice_t result;
    int32_t index;

    if (source == NULL) {
        return NULL;
    }
    result = (tinypy_ast_slice_t)tinypy_internal_compiler_ast_allocate(meta->compile, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    *result = *source;
    if (source->kind == TINYPY_AST_KIND_SLICE) {
        result->v.Slice.lower = __tinypy_meta_clone_expression(meta, source->v.Slice.lower);
        result->v.Slice.upper = __tinypy_meta_clone_expression(meta, source->v.Slice.upper);
        result->v.Slice.step = __tinypy_meta_clone_expression(meta, source->v.Slice.step);
    }
    else if (source->kind == TINYPY_AST_KIND_INDEX) {
        result->v.Index.value = __tinypy_meta_clone_expression(meta, source->v.Index.value);
    }
    else if (source->kind == TINYPY_AST_KIND_EXT_SLICE) {
        result->v.ExtSlice.dims = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(source->v.ExtSlice.dims), meta->compile);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source->v.ExtSlice.dims); ++index) {
            TINYPY_AST_SEQUENCE_SET(result->v.ExtSlice.dims, index, __tinypy_meta_clone_slice(meta, (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(source->v.ExtSlice.dims, index)));
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_arguments_t __tinypy_meta_clone_arguments(tinypy_meta_context_t *meta, tinypy_ast_arguments_t source) {
    tinypy_ast_sequence_t *meta_clone_expression_sequence = __tinypy_meta_clone_expression_sequence(meta, source->args);
    tinypy_ast_sequence_t *meta_clone_expression_sequence_2 = __tinypy_meta_clone_expression_sequence(meta, source->defaults);
    tinypy_ast_arguments_t return_value_1 = __tinypy_ast_arguments(meta_clone_expression_sequence, source->vararg, source->kwarg, meta_clone_expression_sequence_2, meta->compile);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_meta_clone_comprehensions(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *source) {
    int32_t index;

    tinypy_ast_sequence_t *result = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(source), meta->compile);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source); ++index) {
        tinypy_ast_comprehension_t comprehension = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(source, index);
        tinypy_ast_comprehension_t clone;
        tinypy_ast_expression_t target;
        tinypy_ast_expression_t iterator;
        tinypy_ast_sequence_t *conditions;

        if (__tinypy_meta_generated_node(meta, 1, 0) == 0) {
            return NULL;
        }
        target = __tinypy_meta_clone_expression(meta, comprehension->target);
        iterator = __tinypy_meta_clone_expression(meta, comprehension->iter);
        conditions = __tinypy_meta_clone_expression_sequence(meta, comprehension->ifs);
        if (target == NULL || iterator == NULL || (conditions == NULL && comprehension->ifs != NULL)) {
            return NULL;
        }
        clone = __tinypy_ast_comprehension(target, iterator, conditions, meta->compile);
        if (clone == NULL || meta->compile->failed != 0) {
            return NULL;
        }
        TINYPY_AST_SEQUENCE_SET(result, index, clone);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_expression_t __tinypy_meta_clone_expression(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression) {
    tinypy_ast_expression_t result;
    int32_t index;

    if (expression == NULL || meta->compile->failed != 0) {
        return NULL;
    }
    if (__tinypy_meta_generated_node(meta, expression->lineno, expression->col_offset) == 0) {
        return NULL;
    }
    if (expression->kind == TINYPY_AST_KIND_NAME && expression->v.Name.ctx == TINYPY_AST_CONTEXT_LOAD) {
        tinypy_value_t *value = __tinypy_meta_binding_find(meta, expression->v.Name.id);

        if (value != NULL) {
            int32_t meta_generated_line_2 = __tinypy_meta_generated_line(meta, expression->lineno);
            int32_t meta_generated_column_2 = __tinypy_meta_generated_column(meta, expression->col_offset);
            tinypy_ast_expression_t return_value_1 = __tinypy_ast_num(value, meta_generated_line_2, meta_generated_column_2, meta->compile);
            return return_value_1;
        }
    }
    if (__tinypy_meta_call_expression(expression, "current_class", 13U) != 0) {
        if (TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.args) != 0 || meta->current_class == NULL) {
            (void)__tinypy_meta_fail(meta, "meta.current_class() is only valid inside an emitted class", expression->lineno, expression->col_offset);
            return NULL;
        }
        int32_t meta_generated_line = __tinypy_meta_generated_line(meta, expression->lineno);
        int32_t meta_generated_column = __tinypy_meta_generated_column(meta, expression->col_offset);
        tinypy_ast_expression_t return_value_2 = __tinypy_ast_name(meta->current_class, TINYPY_AST_CONTEXT_LOAD, meta_generated_line, meta_generated_column, meta->compile);
        return return_value_2;
    }
    if (__tinypy_meta_call_expression(expression, "name", 4U) != 0 || __tinypy_meta_call_expression(expression, "getattr", 7U) != 0) {
        tinypy_bool_t is_name = __tinypy_meta_call_expression(expression, "name", 4U);
        int32_t expected = is_name != 0 ? 1 : 2;
        tinypy_value_t *name_value;
        tinypy_ast_identifier_t name;

        if (TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.args) != expected) {
            (void)__tinypy_meta_fail(meta, "invalid generated name intrinsic arguments", expression->lineno, expression->col_offset);
            return NULL;
        }
        name_value = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, expected - 1));
        if (name_value == NULL) {
            return NULL;
        }
        name = __tinypy_meta_identifier_from_value(meta, name_value, expression->lineno, expression->col_offset);
        if (name == NULL) {
            return NULL;
        }
        if (is_name != 0) {
            int32_t meta_generated_line_2 = __tinypy_meta_generated_line(meta, expression->lineno);
            int32_t meta_generated_column_2 = __tinypy_meta_generated_column(meta, expression->col_offset);
            tinypy_ast_expression_t return_value_3 = __tinypy_ast_name(name, TINYPY_AST_CONTEXT_LOAD, meta_generated_line_2, meta_generated_column_2, meta->compile);
            return return_value_3;
        }
        tinypy_ast_expression_t meta_clone_expression = __tinypy_meta_clone_expression(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, 0));
        int32_t meta_generated_line = __tinypy_meta_generated_line(meta, expression->lineno);
        int32_t meta_generated_column = __tinypy_meta_generated_column(meta, expression->col_offset);
        tinypy_ast_expression_t return_value_4 = __tinypy_ast_attribute(meta_clone_expression, name, TINYPY_AST_CONTEXT_LOAD, meta_generated_line, meta_generated_column, meta->compile);
        return return_value_4;
    }
    if (__tinypy_meta_call_expression(expression, "concat", 6U) != 0) {
        tinypy_value_t *value = __tinypy_meta_eval(meta, expression);
        if (value == NULL) {
            return NULL;
        }
        int32_t meta_generated_line = __tinypy_meta_generated_line(meta, expression->lineno);
        int32_t meta_generated_column = __tinypy_meta_generated_column(meta, expression->col_offset);
        tinypy_ast_expression_t return_value_5 = __tinypy_ast_num(value, meta_generated_line, meta_generated_column, meta->compile);
        return return_value_5;
    }
    result = (tinypy_ast_expression_t)tinypy_internal_compiler_ast_allocate(meta->compile, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    *result = *expression;
    result->lineno = __tinypy_meta_generated_line(meta, expression->lineno);
    result->col_offset = __tinypy_meta_generated_column(meta, expression->col_offset);
    switch (expression->kind) {
    case TINYPY_AST_KIND_BOOL_OP:
        result->v.BoolOp.values = __tinypy_meta_clone_expression_sequence(meta, expression->v.BoolOp.values);
        break;
    case TINYPY_AST_KIND_BIN_OP:
        result->v.BinOp.left = __tinypy_meta_clone_expression(meta, expression->v.BinOp.left);
        result->v.BinOp.right = __tinypy_meta_clone_expression(meta, expression->v.BinOp.right);
        break;
    case TINYPY_AST_KIND_UNARY_OP:
        result->v.UnaryOp.operand = __tinypy_meta_clone_expression(meta, expression->v.UnaryOp.operand);
        break;
    case TINYPY_AST_KIND_LAMBDA:
        result->v.Lambda.args = __tinypy_meta_clone_arguments(meta, expression->v.Lambda.args);
        result->v.Lambda.body = __tinypy_meta_clone_expression(meta, expression->v.Lambda.body);
        break;
    case TINYPY_AST_KIND_IF_EXP:
        result->v.IfExp.test = __tinypy_meta_clone_expression(meta, expression->v.IfExp.test);
        result->v.IfExp.body = __tinypy_meta_clone_expression(meta, expression->v.IfExp.body);
        result->v.IfExp.orelse = __tinypy_meta_clone_expression(meta, expression->v.IfExp.orelse);
        break;
    case TINYPY_AST_KIND_DICT:
        result->v.Dict.keys = __tinypy_meta_clone_expression_sequence(meta, expression->v.Dict.keys);
        result->v.Dict.values = __tinypy_meta_clone_expression_sequence(meta, expression->v.Dict.values);
        break;
    case TINYPY_AST_KIND_SET:
        result->v.Set.elts = __tinypy_meta_clone_expression_sequence(meta, expression->v.Set.elts);
        break;
    case TINYPY_AST_KIND_YIELD:
        result->v.Yield.value = __tinypy_meta_clone_expression(meta, expression->v.Yield.value);
        break;
    case TINYPY_AST_KIND_COMPARE:
        result->v.Compare.left = __tinypy_meta_clone_expression(meta, expression->v.Compare.left);
        result->v.Compare.ops = __tinypy_meta_clone_integer_sequence(meta, expression->v.Compare.ops);
        result->v.Compare.comparators = __tinypy_meta_clone_expression_sequence(meta, expression->v.Compare.comparators);
        break;
    case TINYPY_AST_KIND_CALL:
        result->v.Call.func = __tinypy_meta_clone_expression(meta, expression->v.Call.func);
        result->v.Call.args = __tinypy_meta_clone_expression_sequence(meta, expression->v.Call.args);
        result->v.Call.starargs = __tinypy_meta_clone_expression(meta, expression->v.Call.starargs);
        result->v.Call.kwargs = __tinypy_meta_clone_expression(meta, expression->v.Call.kwargs);
        result->v.Call.keywords = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords), meta->compile);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords); ++index) {
            tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.keywords, index);
            TINYPY_AST_SEQUENCE_SET(result->v.Call.keywords, index, __tinypy_ast_keyword(keyword->arg, __tinypy_meta_clone_expression(meta, keyword->value), meta->compile));
        }
        break;
    case TINYPY_AST_KIND_REPR:
        result->v.Repr.value = __tinypy_meta_clone_expression(meta, expression->v.Repr.value);
        break;
    case TINYPY_AST_KIND_ATTRIBUTE:
        result->v.Attribute.value = __tinypy_meta_clone_expression(meta, expression->v.Attribute.value);
        break;
    case TINYPY_AST_KIND_SUBSCRIPT:
        result->v.Subscript.value = __tinypy_meta_clone_expression(meta, expression->v.Subscript.value);
        result->v.Subscript.slice = __tinypy_meta_clone_slice(meta, expression->v.Subscript.slice);
        break;
    case TINYPY_AST_KIND_LIST:
        result->v.List.elts = __tinypy_meta_clone_expression_sequence(meta, expression->v.List.elts);
        break;
    case TINYPY_AST_KIND_TUPLE:
        result->v.Tuple.elts = __tinypy_meta_clone_expression_sequence(meta, expression->v.Tuple.elts);
        break;
    case TINYPY_AST_KIND_LIST_COMP:
        result->v.ListComp.elt = __tinypy_meta_clone_expression(meta, expression->v.ListComp.elt);
        result->v.ListComp.generators = __tinypy_meta_clone_comprehensions(meta, expression->v.ListComp.generators);
        break;
    case TINYPY_AST_KIND_SET_COMP:
        result->v.SetComp.elt = __tinypy_meta_clone_expression(meta, expression->v.SetComp.elt);
        result->v.SetComp.generators = __tinypy_meta_clone_comprehensions(meta, expression->v.SetComp.generators);
        break;
    case TINYPY_AST_KIND_DICT_COMP:
        result->v.DictComp.key = __tinypy_meta_clone_expression(meta, expression->v.DictComp.key);
        result->v.DictComp.value = __tinypy_meta_clone_expression(meta, expression->v.DictComp.value);
        result->v.DictComp.generators = __tinypy_meta_clone_comprehensions(meta, expression->v.DictComp.generators);
        break;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        result->v.GeneratorExp.elt = __tinypy_meta_clone_expression(meta, expression->v.GeneratorExp.elt);
        result->v.GeneratorExp.generators = __tinypy_meta_clone_comprehensions(meta, expression->v.GeneratorExp.generators);
        break;
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
    case TINYPY_AST_KIND_NAME:
        break;
    default:
        break;
    }
    return meta->compile->failed == 0 ? result : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_decorator(tinypy_ast_expression_t decorator, const char *name, size_t name_size) {
    if (__tinypy_meta_attribute_expression(decorator, name, name_size) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = decorator != NULL && decorator->kind == TINYPY_AST_KIND_CALL && __tinypy_meta_attribute_expression(decorator->v.Call.func, name, name_size) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_identifier_t __tinypy_meta_decorated_name(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *decorators, tinypy_ast_identifier_t fallback, const char *decorator_name, size_t decorator_name_size, int32_t line, int32_t column) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(decorators); ++index) {
        tinypy_ast_expression_t decorator = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(decorators, index);
        if (__tinypy_meta_decorator(decorator, decorator_name, decorator_name_size) != 0) {
            tinypy_ast_expression_t value_expression = NULL;
            tinypy_value_t *value;

            if (decorator->kind != TINYPY_AST_KIND_CALL) {
                return fallback;
            }
            if (TINYPY_AST_SEQUENCE_LENGTH(decorator->v.Call.args) == 1) {
                value_expression = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(decorator->v.Call.args, 0);
            }
            else if (TINYPY_AST_SEQUENCE_LENGTH(decorator->v.Call.args) == 0 && TINYPY_AST_SEQUENCE_LENGTH(decorator->v.Call.keywords) == 1) {
                tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(decorator->v.Call.keywords, 0);
                if (__tinypy_meta_identifier_equal(keyword->arg, "name", 4U) != 0) {
                    value_expression = keyword->value;
                }
            }
            if (value_expression == NULL) {
                (void)__tinypy_meta_fail(meta, "meta decorator expects one name argument", line, column);
                return NULL;
            }
            value = __tinypy_meta_eval(meta, value_expression);
            tinypy_ast_identifier_t return_value_1 = value != NULL ? __tinypy_meta_identifier_from_value(meta, value, line, column) : NULL;
            return return_value_1;
        }
    }
    return fallback;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_meta_clone_decorators(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *source) {
    int32_t index;
    int32_t count = 0;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source); ++index) {
        tinypy_ast_expression_t decorator = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(source, index);
        if (__tinypy_meta_decorator(decorator, "emit", 4U) == 0 && __tinypy_meta_decorator(decorator, "rename", 6U) == 0) {
            count += 1;
        }
    }
    tinypy_ast_sequence_t *result = TINYPY_AST_SEQUENCE_NEW(count, meta->compile);
    count = 0;
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source); ++index) {
        tinypy_ast_expression_t decorator = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(source, index);
        if (__tinypy_meta_decorator(decorator, "emit", 4U) == 0 && __tinypy_meta_decorator(decorator, "rename", 6U) == 0) {
            TINYPY_AST_SEQUENCE_SET(result, count, __tinypy_meta_clone_expression(meta, decorator));
            count += 1;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_meta_clone_statement_sequence(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *source) {
    int32_t index;

    if (source == NULL) {
        return NULL;
    }
    tinypy_ast_sequence_t *result = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(source), meta->compile);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source); ++index) {
        TINYPY_AST_SEQUENCE_SET(result, index, __tinypy_meta_clone_statement(meta, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(source, index)));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_statement_t __tinypy_meta_clone_statement(tinypy_meta_context_t *meta, tinypy_ast_statement_t statement) {
    tinypy_ast_statement_t result;
    tinypy_ast_identifier_t previous_class = meta->current_class;
    int32_t index;

    if (statement == NULL) {
        return NULL;
    }
    if (__tinypy_meta_generated_node(meta, statement->lineno, statement->col_offset) == 0) {
        return NULL;
    }
    result = (tinypy_ast_statement_t)tinypy_internal_compiler_ast_allocate(meta->compile, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    *result = *statement;
    result->lineno = __tinypy_meta_generated_line(meta, statement->lineno);
    result->col_offset = __tinypy_meta_generated_column(meta, statement->col_offset);
    switch (statement->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        result->v.FunctionDef.name = __tinypy_meta_decorated_name(meta, statement->v.FunctionDef.decorator_list, statement->v.FunctionDef.name, "rename", 6U, statement->lineno, statement->col_offset);
        result->v.FunctionDef.args = __tinypy_meta_clone_arguments(meta, statement->v.FunctionDef.args);
        result->v.FunctionDef.decorator_list = __tinypy_meta_clone_decorators(meta, statement->v.FunctionDef.decorator_list);
        result->v.FunctionDef.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.FunctionDef.body);
        break;
    case TINYPY_AST_KIND_CLASS_DEF:
        result->v.ClassDef.name = __tinypy_meta_decorated_name(meta, statement->v.ClassDef.decorator_list, statement->v.ClassDef.name, "rename", 6U, statement->lineno, statement->col_offset);
        meta->current_class = result->v.ClassDef.name;
        result->v.ClassDef.bases = __tinypy_meta_clone_expression_sequence(meta, statement->v.ClassDef.bases);
        result->v.ClassDef.decorator_list = __tinypy_meta_clone_decorators(meta, statement->v.ClassDef.decorator_list);
        result->v.ClassDef.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.ClassDef.body);
        meta->current_class = previous_class;
        break;
    case TINYPY_AST_KIND_RETURN:
        result->v.Return.value = __tinypy_meta_clone_expression(meta, statement->v.Return.value);
        break;
    case TINYPY_AST_KIND_DELETE:
        result->v.Delete.targets = __tinypy_meta_clone_expression_sequence(meta, statement->v.Delete.targets);
        break;
    case TINYPY_AST_KIND_ASSIGN:
        result->v.Assign.targets = __tinypy_meta_clone_expression_sequence(meta, statement->v.Assign.targets);
        result->v.Assign.value = __tinypy_meta_clone_expression(meta, statement->v.Assign.value);
        break;
    case TINYPY_AST_KIND_AUG_ASSIGN:
        result->v.AugAssign.target = __tinypy_meta_clone_expression(meta, statement->v.AugAssign.target);
        result->v.AugAssign.value = __tinypy_meta_clone_expression(meta, statement->v.AugAssign.value);
        break;
    case TINYPY_AST_KIND_EXPR:
        if (__tinypy_meta_call_expression(statement->v.Expr.value, "setattr", 7U) != 0 || __tinypy_meta_call_expression(statement->v.Expr.value, "delattr", 7U) != 0) {
            tinypy_ast_expression_t call = statement->v.Expr.value;
            tinypy_bool_t is_set = __tinypy_meta_call_expression(call, "setattr", 7U);
            int32_t expected = is_set != 0 ? 3 : 2;
            tinypy_value_t *name_value;
            tinypy_ast_identifier_t name;
            tinypy_ast_expression_t attribute;
            tinypy_ast_sequence_t *targets;

            if (TINYPY_AST_SEQUENCE_LENGTH(call->v.Call.args) != expected) {
                (void)__tinypy_meta_fail(meta, "invalid meta attribute intrinsic arguments", statement->lineno, statement->col_offset);
                return NULL;
            }
            name_value = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.args, 1));
            name = name_value != NULL ? __tinypy_meta_identifier_from_value(meta, name_value, statement->lineno, statement->col_offset) : NULL;
            if (name == NULL) {
                return NULL;
            }
            tinypy_ast_expression_t meta_clone_expression = __tinypy_meta_clone_expression(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.args, 0));
            attribute = __tinypy_ast_attribute(meta_clone_expression, name, is_set != 0 ? TINYPY_AST_CONTEXT_STORE : TINYPY_AST_CONTEXT_DELETE, result->lineno, result->col_offset, meta->compile);
            targets = TINYPY_AST_SEQUENCE_NEW(1, meta->compile);
            TINYPY_AST_SEQUENCE_SET(targets, 0, attribute);
            tinypy_ast_statement_t selected_value;
            if (is_set != 0) {
                tinypy_ast_expression_t meta_clone_expression_2 = __tinypy_meta_clone_expression(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.args, 2));
                selected_value = __tinypy_ast_assign(targets, meta_clone_expression_2, result->lineno, result->col_offset, meta->compile);
            }
            else {
                selected_value = __tinypy_ast_delete(targets, result->lineno, result->col_offset, meta->compile);
            }
            return selected_value;
        }
        result->v.Expr.value = __tinypy_meta_clone_expression(meta, statement->v.Expr.value);
        break;
    case TINYPY_AST_KIND_IF:
        result->v.If.test = __tinypy_meta_clone_expression(meta, statement->v.If.test);
        result->v.If.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.If.body);
        result->v.If.orelse = __tinypy_meta_clone_statement_sequence(meta, statement->v.If.orelse);
        break;
    case TINYPY_AST_KIND_FOR:
        result->v.For.target = __tinypy_meta_clone_expression(meta, statement->v.For.target);
        result->v.For.iter = __tinypy_meta_clone_expression(meta, statement->v.For.iter);
        result->v.For.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.For.body);
        result->v.For.orelse = __tinypy_meta_clone_statement_sequence(meta, statement->v.For.orelse);
        break;
    case TINYPY_AST_KIND_WHILE:
        result->v.While.test = __tinypy_meta_clone_expression(meta, statement->v.While.test);
        result->v.While.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.While.body);
        result->v.While.orelse = __tinypy_meta_clone_statement_sequence(meta, statement->v.While.orelse);
        break;
    case TINYPY_AST_KIND_WITH:
        result->v.With.context_expr = __tinypy_meta_clone_expression(meta, statement->v.With.context_expr);
        result->v.With.optional_vars = __tinypy_meta_clone_expression(meta, statement->v.With.optional_vars);
        result->v.With.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.With.body);
        break;
    case TINYPY_AST_KIND_RAISE:
        result->v.Raise.type = __tinypy_meta_clone_expression(meta, statement->v.Raise.type);
        result->v.Raise.inst = __tinypy_meta_clone_expression(meta, statement->v.Raise.inst);
        result->v.Raise.tback = __tinypy_meta_clone_expression(meta, statement->v.Raise.tback);
        break;
    case TINYPY_AST_KIND_TRY_EXCEPT:
        result->v.TryExcept.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.TryExcept.body);
        result->v.TryExcept.orelse = __tinypy_meta_clone_statement_sequence(meta, statement->v.TryExcept.orelse);
        result->v.TryExcept.handlers = TINYPY_AST_SEQUENCE_NEW(TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.handlers), meta->compile);
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.handlers); ++index) {
            tinypy_ast_exception_handler_t handler = (tinypy_ast_exception_handler_t)TINYPY_AST_SEQUENCE_GET(statement->v.TryExcept.handlers, index);
            TINYPY_AST_SEQUENCE_SET(result->v.TryExcept.handlers, index, __tinypy_ast_except_handler(__tinypy_meta_clone_expression(meta, handler->v.ExceptHandler.type), __tinypy_meta_clone_expression(meta, handler->v.ExceptHandler.name), __tinypy_meta_clone_statement_sequence(meta, handler->v.ExceptHandler.body), result->lineno, result->col_offset, meta->compile));
        }
        break;
    case TINYPY_AST_KIND_TRY_FINALLY:
        result->v.TryFinally.body = __tinypy_meta_clone_statement_sequence(meta, statement->v.TryFinally.body);
        result->v.TryFinally.finalbody = __tinypy_meta_clone_statement_sequence(meta, statement->v.TryFinally.finalbody);
        break;
    case TINYPY_AST_KIND_ASSERT:
        result->v.Assert.test = __tinypy_meta_clone_expression(meta, statement->v.Assert.test);
        result->v.Assert.msg = __tinypy_meta_clone_expression(meta, statement->v.Assert.msg);
        break;
    case TINYPY_AST_KIND_EXEC:
        result->v.Exec.body = __tinypy_meta_clone_expression(meta, statement->v.Exec.body);
        result->v.Exec.globals = __tinypy_meta_clone_expression(meta, statement->v.Exec.globals);
        result->v.Exec.locals = __tinypy_meta_clone_expression(meta, statement->v.Exec.locals);
        break;
    case TINYPY_AST_KIND_PRINT:
        result->v.Print.dest = __tinypy_meta_clone_expression(meta, statement->v.Print.dest);
        result->v.Print.values = __tinypy_meta_clone_expression_sequence(meta, statement->v.Print.values);
        break;
    case TINYPY_AST_KIND_IMPORT:
    case TINYPY_AST_KIND_IMPORT_FROM:
    case TINYPY_AST_KIND_GLOBAL:
    case TINYPY_AST_KIND_PASS:
    case TINYPY_AST_KIND_BREAK:
    case TINYPY_AST_KIND_CONTINUE:
        break;
    default:
        break;
    }
    return meta->compile->failed == 0 ? result : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_builder_append(tinypy_meta_context_t *meta, tinypy_meta_statement_builder_t *builder, tinypy_ast_statement_t statement) {
    if (statement == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_meta_statement_node_t *node = (tinypy_meta_statement_node_t *)tinypy_internal_compiler_arena_allocate(meta->compile, sizeof(*node));
    if (node == NULL) {
        return TINYPY_FALSE;
    }
    node->statement = statement;
    node->template_line = 0;
    node->template_column = 0;
    if (builder->tail != NULL) {
        builder->tail->next = node;
    }
    else {
        builder->head = node;
    }
    builder->tail = node;
    builder->size += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_source_map_add(tinypy_meta_context_t *meta, tinypy_ast_statement_t statement, tinypy_ast_identifier_t symbol, int32_t template_line, int32_t template_column, int32_t expansion_line, int32_t expansion_column) {
    if (meta->compile->limits.max_source_map_entries != 0U && meta->compile->source_map_entries >= meta->compile->limits.max_source_map_entries) {
        tinypy_bool_t return_value_1 = __tinypy_meta_limit(meta, "source map entry limit exceeded", expansion_line, expansion_column);
        return return_value_1;
    }
    tinypy_source_map_record_t *record = (tinypy_source_map_record_t *)tinypy_internal_compiler_arena_allocate(meta->compile, sizeof(*record));
    if (record == NULL) {
        return TINYPY_FALSE;
    }
    record->statement = statement;
    record->symbol = symbol;
    record->template_line = template_line;
    record->template_column = template_column;
    record->expansion_line = expansion_line;
    record->expansion_column = expansion_column;
    record->next = meta->compile->source_map_records;
    meta->compile->source_map_records = record;
    meta->compile->source_map_entries += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_meta_template_t *__tinypy_meta_template_find(tinypy_meta_context_t *meta, tinypy_ast_identifier_t name) {
    tinypy_meta_template_t *item = meta->templates;

    while (item != NULL) {
        size_t left_size;
        size_t right_size;
        const void *left = tinypy_string_view(item->name, &left_size);
        const void *right = tinypy_string_view(name, &right_size);
        if (left_size == right_size && (left_size == 0U || memcmp(left, right, left_size) == 0)) {
            return item;
        }
        item = item->next;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_is_template(tinypy_ast_statement_t statement) {
    tinypy_bool_t return_value_1 = statement->kind == TINYPY_AST_KIND_FUNCTION_DEF && TINYPY_AST_SEQUENCE_LENGTH(statement->v.FunctionDef.decorator_list) == 1 && __tinypy_meta_attribute_expression((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(statement->v.FunctionDef.decorator_list, 0), "template", 8U) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_is_emit(tinypy_ast_statement_t statement) {
    int32_t index;

    if (statement->kind != TINYPY_AST_KIND_FUNCTION_DEF && statement->kind != TINYPY_AST_KIND_CLASS_DEF) {
        return TINYPY_FALSE;
    }
    tinypy_ast_sequence_t *decorators = statement->kind == TINYPY_AST_KIND_FUNCTION_DEF ? statement->v.FunctionDef.decorator_list : statement->v.ClassDef.decorator_list;
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(decorators); ++index) {
        if (__tinypy_meta_decorator((tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(decorators, index), "emit", 4U) != 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_execute_sequence(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *sequence, tinypy_meta_statement_builder_t *emitted) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        tinypy_ast_statement_t statement = (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(sequence, index);

        if (__tinypy_meta_tick(meta, statement->lineno, statement->col_offset) == 0) {
            return TINYPY_FALSE;
        }
        if (statement->kind == TINYPY_AST_KIND_ASSIGN) {
            tinypy_ast_expression_t target;
            tinypy_value_t *value;

            if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.Assign.targets) != 1) {
                tinypy_bool_t return_value_1 = __tinypy_meta_fail(meta, "meta assignment expects one local name", statement->lineno, statement->col_offset);
                return return_value_1;
            }
            target = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(statement->v.Assign.targets, 0);
            if (target->kind != TINYPY_AST_KIND_NAME) {
                tinypy_bool_t return_value_2 = __tinypy_meta_fail(meta, "meta assignment target must be a local name", statement->lineno, statement->col_offset);
                return return_value_2;
            }
            value = __tinypy_meta_eval(meta, statement->v.Assign.value);
            if (value == NULL || __tinypy_meta_binding_set(meta, target->v.Name.id, value) == 0) {
                return TINYPY_FALSE;
            }
        }
        else if (statement->kind == TINYPY_AST_KIND_IF) {
            tinypy_value_t *condition = __tinypy_meta_eval(meta, statement->v.If.test);
            tinypy_error_t *error = NULL;
            int32_t truth;

            if (condition == NULL) {
                return TINYPY_FALSE;
            }
            truth = tinypy_truth(condition, &error);
            if (truth < 0) {
                (void)__tinypy_meta_eval_error(meta, error, statement->v.If.test);
                return TINYPY_FALSE;
            }
            if (__tinypy_meta_execute_sequence(meta, truth != 0 ? statement->v.If.body : statement->v.If.orelse, emitted) == 0) {
                return TINYPY_FALSE;
            }
        }
        else if (statement->kind == TINYPY_AST_KIND_FOR) {
            tinypy_value_t *iterable;
            tinypy_ast_expression_t target = statement->v.For.target;
            size_t count;
            size_t item_index;

            if (target->kind != TINYPY_AST_KIND_NAME) {
                tinypy_bool_t return_value_3 = __tinypy_meta_fail(meta, "meta for target must be a local name", statement->lineno, statement->col_offset);
                return return_value_3;
            }
            iterable = __tinypy_meta_eval(meta, statement->v.For.iter);
            if (iterable == NULL) {
                return TINYPY_FALSE;
            }
            if (tinypy_typeof(iterable) == TINYPY_VALUE_TUPLE) {
                count = TINYPY_TUPLE_SIZE(iterable);
            }
            else if (tinypy_typeof(iterable) == TINYPY_VALUE_LIST) {
                count = TINYPY_LIST_SIZE(iterable);
            }
            else {
                tinypy_bool_t return_value_4 = __tinypy_meta_fail(meta, "meta for iterable must be tuple or meta.range", statement->lineno, statement->col_offset);
                return return_value_4;
            }
            for (item_index = 0U; item_index < count; ++item_index) {
                tinypy_value_t *item = tinypy_typeof(iterable) == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(iterable, item_index) : TINYPY_LIST_GET(iterable, item_index);
                if (__tinypy_meta_binding_set(meta, target->v.Name.id, item) == 0 || __tinypy_meta_execute_sequence(meta, statement->v.For.body, emitted) == 0) {
                    return TINYPY_FALSE;
                }
            }
            if (__tinypy_meta_execute_sequence(meta, statement->v.For.orelse, emitted) == 0) {
                return TINYPY_FALSE;
            }
        }
        else if (__tinypy_meta_is_emit(statement) != 0) {
            tinypy_ast_sequence_t *decorators = statement->kind == TINYPY_AST_KIND_FUNCTION_DEF ? statement->v.FunctionDef.decorator_list : statement->v.ClassDef.decorator_list;
            tinypy_ast_identifier_t fallback = statement->kind == TINYPY_AST_KIND_FUNCTION_DEF ? statement->v.FunctionDef.name : statement->v.ClassDef.name;
            tinypy_ast_identifier_t name = __tinypy_meta_decorated_name(meta, decorators, fallback, "emit", 4U, statement->lineno, statement->col_offset);
            tinypy_ast_identifier_t original_name = fallback;
            tinypy_ast_statement_t clone;

            if (name == NULL) {
                return TINYPY_FALSE;
            }
            if (statement->kind == TINYPY_AST_KIND_FUNCTION_DEF) {
                statement->v.FunctionDef.name = name;
            }
            else {
                statement->v.ClassDef.name = name;
            }
            clone = __tinypy_meta_clone_statement(meta, statement);
            if (statement->kind == TINYPY_AST_KIND_FUNCTION_DEF) {
                statement->v.FunctionDef.name = original_name;
            }
            else {
                statement->v.ClassDef.name = original_name;
            }
            if (clone == NULL) {
                return TINYPY_FALSE;
            }
            if (__tinypy_meta_builder_append(meta, emitted, clone) == 0) {
                return TINYPY_FALSE;
            }
            emitted->tail->template_line = statement->lineno;
            emitted->tail->template_column = statement->col_offset;
        }
        else if (statement->kind != TINYPY_AST_KIND_PASS && !(statement->kind == TINYPY_AST_KIND_EXPR && statement->v.Expr.value->kind == TINYPY_AST_KIND_STR)) {
            tinypy_bool_t return_value_5 = __tinypy_meta_fail(meta, "statement is not allowed in a meta template", statement->lineno, statement->col_offset);
            return return_value_5;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_bind_arguments(tinypy_meta_context_t *meta, tinypy_ast_statement_t definition, tinypy_ast_expression_t call) {
    tinypy_ast_arguments_t arguments = definition->v.FunctionDef.args;
    int32_t parameter_count = TINYPY_AST_SEQUENCE_LENGTH(arguments->args);
    int32_t default_count = TINYPY_AST_SEQUENCE_LENGTH(arguments->defaults);
    int32_t positional_count = TINYPY_AST_SEQUENCE_LENGTH(call->v.Call.args) - 1;
    int32_t index;

    if (arguments->vararg != NULL || arguments->kwarg != NULL || call->v.Call.starargs != NULL || call->v.Call.kwargs != NULL || positional_count > parameter_count) {
        tinypy_bool_t return_value_1 = __tinypy_meta_fail(meta, "invalid meta template arguments", call->lineno, call->col_offset);
        return return_value_1;
    }
    for (index = 0; index < parameter_count; ++index) {
        tinypy_ast_expression_t parameter = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(arguments->args, index);
        tinypy_value_t *value = NULL;
        int32_t keyword_index;

        if (index < positional_count) {
            value = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.args, index + 1));
        }
        for (keyword_index = 0; keyword_index < TINYPY_AST_SEQUENCE_LENGTH(call->v.Call.keywords); ++keyword_index) {
            tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.keywords, keyword_index); {
                size_t parameter_size;
                size_t keyword_size;
                const void *parameter_data = tinypy_string_view(parameter->v.Name.id, &parameter_size);
                const void *keyword_data = tinypy_string_view(keyword->arg, &keyword_size);
                if (parameter_size == keyword_size && memcmp(parameter_data, keyword_data, parameter_size) == 0) {
                    if (value != NULL) {
                        tinypy_bool_t return_value_2 = __tinypy_meta_fail(meta, "duplicate meta template argument", call->lineno, call->col_offset);
                        return return_value_2;
                    }
                    value = __tinypy_meta_eval(meta, keyword->value);
                }
            }
        }
        if (value == NULL && index >= parameter_count - default_count) {
            value = __tinypy_meta_eval(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(arguments->defaults, index - (parameter_count - default_count)));
        }
        if (value == NULL) {
            tinypy_bool_t return_value_3 = __tinypy_meta_fail(meta, "missing meta template argument", call->lineno, call->col_offset);
            return return_value_3;
        }
        if (__tinypy_meta_binding_set(meta, parameter->v.Name.id, value) == 0) {
            return TINYPY_FALSE;
        }
    }
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(call->v.Call.keywords); ++index) {
        tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.keywords, index);
        int32_t parameter_index;
        tinypy_bool_t matched = TINYPY_FALSE;

        for (parameter_index = 0; parameter_index < parameter_count; ++parameter_index) {
            tinypy_ast_expression_t parameter = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(arguments->args, parameter_index);
            size_t parameter_size;
            size_t keyword_size;
            const void *parameter_data = tinypy_string_view(parameter->v.Name.id, &parameter_size);
            const void *keyword_data = tinypy_string_view(keyword->arg, &keyword_size);

            if (parameter_size == keyword_size && (parameter_size == 0U || memcmp(parameter_data, keyword_data, parameter_size) == 0)) {
                matched = 1;
                break;
            }
        }
        if (matched == 0) {
            tinypy_bool_t return_value_4 = __tinypy_meta_fail(meta, "unknown meta template keyword argument", call->lineno, call->col_offset);
            return return_value_4;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_expand_call(tinypy_meta_context_t *meta, tinypy_ast_expression_t call, tinypy_meta_statement_builder_t *output, tinypy_ast_expression_t *out_result) {
    tinypy_ast_expression_t template_name;
    tinypy_meta_binding_t *saved_bindings = meta->bindings;
    tinypy_meta_statement_builder_t emitted;
    tinypy_ast_sequence_t *result_items;
    int32_t saved_expansion_line = meta->expansion_line;
    int32_t saved_expansion_column = meta->expansion_column;
    size_t index = 0U;

    if (TINYPY_AST_SEQUENCE_LENGTH(call->v.Call.args) < 1) {
        tinypy_bool_t return_value_1 = __tinypy_meta_fail(meta, "meta.expand expects a template", call->lineno, call->col_offset);
        return return_value_1;
    }
    template_name = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(call->v.Call.args, 0);
    if (template_name->kind != TINYPY_AST_KIND_NAME) {
        tinypy_bool_t return_value_2 = __tinypy_meta_fail(meta, "meta.expand template must be a template name", call->lineno, call->col_offset);
        return return_value_2;
    }
    tinypy_meta_template_t *template_item = __tinypy_meta_template_find(meta, template_name->v.Name.id);
    if (template_item == NULL) {
        tinypy_bool_t return_value_3 = __tinypy_meta_fail(meta, "meta.expand references an unknown template", call->lineno, call->col_offset);
        return return_value_3;
    }
    if (meta->compile->limits.max_template_depth != 0U && meta->expansion_depth >= meta->compile->limits.max_template_depth) {
        tinypy_bool_t return_value_4 = __tinypy_meta_limit(meta, "meta expansion depth limit exceeded", call->lineno, call->col_offset);
        return return_value_4;
    }
    if (meta->compile->limits.max_template_expansions != 0U && meta->compile->template_expansions >= meta->compile->limits.max_template_expansions) {
        tinypy_bool_t return_value_5 = __tinypy_meta_limit(meta, "meta expansion count limit exceeded", call->lineno, call->col_offset);
        return return_value_5;
    }
    meta->compile->template_expansions += 1U;
    meta->expansion_depth += 1U;
    meta->expansion_line = call->lineno;
    meta->expansion_column = call->col_offset;
    meta->bindings = NULL;
    (void)memset(&emitted, 0, sizeof(emitted));
    if (__tinypy_meta_bind_arguments(meta, template_item->definition, call) == 0 || __tinypy_meta_execute_sequence(meta, template_item->definition->v.FunctionDef.body, &emitted) == 0) {
        meta->bindings = saved_bindings;
        meta->expansion_depth -= 1U;
        meta->expansion_line = saved_expansion_line;
        meta->expansion_column = saved_expansion_column;
        return TINYPY_FALSE;
    }
    if (emitted.size == 0U) {
        meta->bindings = saved_bindings;
        meta->expansion_depth -= 1U;
        meta->expansion_line = saved_expansion_line;
        meta->expansion_column = saved_expansion_column;
        tinypy_bool_t return_value_6 = __tinypy_meta_fail(meta, "meta template emitted no declarations", call->lineno, call->col_offset);
        return return_value_6;
    }
    result_items = TINYPY_AST_SEQUENCE_NEW((int32_t)emitted.size, meta->compile);
    if (result_items == NULL) {
        goto failed;
    }
    tinypy_meta_statement_node_t *node = emitted.head;
    while (node != NULL) {
        tinypy_ast_identifier_t name = node->statement->kind == TINYPY_AST_KIND_FUNCTION_DEF ? node->statement->v.FunctionDef.name : node->statement->v.ClassDef.name;
        if (__tinypy_meta_builder_append(meta, output, node->statement) == 0) {
            goto failed;
        }
        if (__tinypy_meta_source_map_add(meta, node->statement, name, node->template_line, node->template_column, call->lineno, call->col_offset) == 0) {
            goto failed;
        }
        TINYPY_AST_SEQUENCE_SET(result_items, (int32_t)index, __tinypy_ast_name(name, TINYPY_AST_CONTEXT_LOAD, call->lineno, call->col_offset, meta->compile));
        index += 1U;
        node = node->next;
    }
    *out_result = emitted.size == 1U ? (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(result_items, 0) : __tinypy_ast_tuple(result_items, TINYPY_AST_CONTEXT_LOAD, call->lineno, call->col_offset, meta->compile);
    meta->bindings = saved_bindings;
    meta->expansion_depth -= 1U;
    meta->expansion_line = saved_expansion_line;
    meta->expansion_column = saved_expansion_column;
    return TINYPY_TRUE;

failed:
    meta->bindings = saved_bindings;
    meta->expansion_depth -= 1U;
    meta->expansion_line = saved_expansion_line;
    meta->expansion_column = saved_expansion_column;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_expand_statement(tinypy_meta_context_t *meta, tinypy_meta_statement_builder_t *output, tinypy_ast_statement_t statement) {
    tinypy_ast_expression_t call = NULL;
    int32_t expression_statement = 0;

    if (statement->kind == TINYPY_AST_KIND_ASSIGN && __tinypy_meta_call_expression(statement->v.Assign.value, "expand", 6U) != 0) {
        call = statement->v.Assign.value;
    }
    else if (statement->kind == TINYPY_AST_KIND_EXPR && __tinypy_meta_call_expression(statement->v.Expr.value, "expand", 6U) != 0) {
        call = statement->v.Expr.value;
        expression_statement = 1;
    }
    if (call != NULL) {
        tinypy_ast_expression_t expansion_result;

        if (__tinypy_meta_expand_call(meta, call, output, &expansion_result) == 0) {
            return TINYPY_FALSE;
        }
        if (expression_statement == 0) {
            statement->v.Assign.value = expansion_result;
            tinypy_bool_t return_value_1 = __tinypy_meta_builder_append(meta, output, statement);
            return return_value_1;
        }
        return TINYPY_TRUE;
    }
    if (statement->kind == TINYPY_AST_KIND_FUNCTION_DEF) {
        statement->v.FunctionDef.body = __tinypy_meta_expand_sequence(meta, statement->v.FunctionDef.body);
    }
    else if (statement->kind == TINYPY_AST_KIND_CLASS_DEF) {
        statement->v.ClassDef.body = __tinypy_meta_expand_sequence(meta, statement->v.ClassDef.body);
    }
    else if (statement->kind == TINYPY_AST_KIND_IF) {
        statement->v.If.body = __tinypy_meta_expand_sequence(meta, statement->v.If.body);
        statement->v.If.orelse = __tinypy_meta_expand_sequence(meta, statement->v.If.orelse);
    }
    else if (statement->kind == TINYPY_AST_KIND_FOR) {
        statement->v.For.body = __tinypy_meta_expand_sequence(meta, statement->v.For.body);
        statement->v.For.orelse = __tinypy_meta_expand_sequence(meta, statement->v.For.orelse);
    }
    else if (statement->kind == TINYPY_AST_KIND_WHILE) {
        statement->v.While.body = __tinypy_meta_expand_sequence(meta, statement->v.While.body);
        statement->v.While.orelse = __tinypy_meta_expand_sequence(meta, statement->v.While.orelse);
    }
    else if (statement->kind == TINYPY_AST_KIND_WITH) {
        statement->v.With.body = __tinypy_meta_expand_sequence(meta, statement->v.With.body);
    }
    else if (statement->kind == TINYPY_AST_KIND_TRY_EXCEPT) {
        statement->v.TryExcept.body = __tinypy_meta_expand_sequence(meta, statement->v.TryExcept.body);
        statement->v.TryExcept.orelse = __tinypy_meta_expand_sequence(meta, statement->v.TryExcept.orelse);
    }
    else if (statement->kind == TINYPY_AST_KIND_TRY_FINALLY) {
        statement->v.TryFinally.body = __tinypy_meta_expand_sequence(meta, statement->v.TryFinally.body);
        statement->v.TryFinally.finalbody = __tinypy_meta_expand_sequence(meta, statement->v.TryFinally.finalbody);
    }
    if (meta->compile->failed != 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_2 = __tinypy_meta_builder_append(meta, output, statement);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_ast_sequence_t *__tinypy_meta_expand_sequence(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *sequence) {
    tinypy_meta_statement_builder_t builder;
    int32_t source_index;
    int32_t result_index = 0;

    (void)memset(&builder, 0, sizeof(builder));
    for (source_index = 0; source_index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++source_index) {
        if (__tinypy_meta_expand_statement(meta, &builder, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(sequence, source_index)) == 0) {
            return NULL;
        }
    }
    if (builder.size == 0U) {
        tinypy_ast_statement_t pass_statement = __tinypy_ast_pass(1, 0, meta->compile);
        if (__tinypy_meta_builder_append(meta, &builder, pass_statement) == 0) {
            return NULL;
        }
    }
    if (builder.size > (size_t)INT_MAX) {
        (void)__tinypy_meta_limit(meta, "expanded suite is too large", 1, 0);
        return NULL;
    }
    tinypy_ast_sequence_t *result = TINYPY_AST_SEQUENCE_NEW((int32_t)builder.size, meta->compile);
    tinypy_meta_statement_node_t *node = builder.head;
    while (node != NULL) {
        TINYPY_AST_SEQUENCE_SET(result, result_index, node->statement);
        result_index += 1;
        node = node->next;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_identifier_validate(tinypy_meta_context_t *meta, tinypy_ast_identifier_t identifier, int32_t line, int32_t column) {
    if (identifier == NULL || __tinypy_meta_identifier_is_builtin(identifier) == 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = __tinypy_meta_fail(meta, "meta is compiler-only and must be consumed during expansion", line, column);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_expression_sequence_validate(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *sequence) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (__tinypy_meta_runtime_expression_validate(meta, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(sequence, index)) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_statement_sequence_validate(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *sequence) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (__tinypy_meta_runtime_statement_validate(meta, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(sequence, index)) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_slice_validate(tinypy_meta_context_t *meta, tinypy_ast_slice_t slice) {
    tinypy_bool_t function_result;
    int32_t index;

    if (slice == NULL) {
        return TINYPY_TRUE;
    }
    switch (slice->kind) {
    case TINYPY_AST_KIND_ELLIPSIS:
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_SLICE:
        function_result = (slice->v.Slice.lower == NULL || __tinypy_meta_runtime_expression_validate(meta, slice->v.Slice.lower) != 0) && (slice->v.Slice.upper == NULL || __tinypy_meta_runtime_expression_validate(meta, slice->v.Slice.upper) != 0) && (slice->v.Slice.step == NULL || __tinypy_meta_runtime_expression_validate(meta, slice->v.Slice.step) != 0);
        return function_result;
    case TINYPY_AST_KIND_EXT_SLICE:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(slice->v.ExtSlice.dims); ++index) {
            if (__tinypy_meta_runtime_slice_validate(meta, (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(slice->v.ExtSlice.dims, index)) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_INDEX:
        function_result = __tinypy_meta_runtime_expression_validate(meta, slice->v.Index.value);
        return function_result;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_arguments_validate(tinypy_meta_context_t *meta, tinypy_ast_arguments_t arguments, int32_t line, int32_t column) {
    tinypy_bool_t return_value_1 = __tinypy_meta_runtime_expression_sequence_validate(meta, arguments->args) && __tinypy_meta_runtime_identifier_validate(meta, arguments->vararg, line, column) && __tinypy_meta_runtime_identifier_validate(meta, arguments->kwarg, line, column) && __tinypy_meta_runtime_expression_sequence_validate(meta, arguments->defaults);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_comprehensions_validate(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *generators) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(generators); ++index) {
        tinypy_ast_comprehension_t generator = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, index);

        if (__tinypy_meta_runtime_expression_validate(meta, generator->target) == 0 || __tinypy_meta_runtime_expression_validate(meta, generator->iter) == 0 || __tinypy_meta_runtime_expression_sequence_validate(meta, generator->ifs) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_expression_validate(tinypy_meta_context_t *meta, tinypy_ast_expression_t expression) {
    tinypy_bool_t function_result;
    int32_t index;

    if (expression == NULL) {
        return TINYPY_TRUE;
    }
    switch (expression->kind) {
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_NAME:
        function_result = __tinypy_meta_runtime_identifier_validate(meta, expression->v.Name.id, expression->lineno, expression->col_offset);
        return function_result;
    case TINYPY_AST_KIND_BOOL_OP:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.BoolOp.values);
        return function_result;
    case TINYPY_AST_KIND_BIN_OP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.BinOp.left) && __tinypy_meta_runtime_expression_validate(meta, expression->v.BinOp.right);
        return function_result;
    case TINYPY_AST_KIND_UNARY_OP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.UnaryOp.operand);
        return function_result;
    case TINYPY_AST_KIND_LAMBDA:
        function_result = __tinypy_meta_runtime_arguments_validate(meta, expression->v.Lambda.args, expression->lineno, expression->col_offset) && __tinypy_meta_runtime_expression_validate(meta, expression->v.Lambda.body);
        return function_result;
    case TINYPY_AST_KIND_IF_EXP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.IfExp.test) && __tinypy_meta_runtime_expression_validate(meta, expression->v.IfExp.body) && __tinypy_meta_runtime_expression_validate(meta, expression->v.IfExp.orelse);
        return function_result;
    case TINYPY_AST_KIND_DICT:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.Dict.keys) && __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.Dict.values);
        return function_result;
    case TINYPY_AST_KIND_SET:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.Set.elts);
        return function_result;
    case TINYPY_AST_KIND_LIST_COMP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.ListComp.elt) && __tinypy_meta_runtime_comprehensions_validate(meta, expression->v.ListComp.generators);
        return function_result;
    case TINYPY_AST_KIND_SET_COMP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.SetComp.elt) && __tinypy_meta_runtime_comprehensions_validate(meta, expression->v.SetComp.generators);
        return function_result;
    case TINYPY_AST_KIND_DICT_COMP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.DictComp.key) && __tinypy_meta_runtime_expression_validate(meta, expression->v.DictComp.value) && __tinypy_meta_runtime_comprehensions_validate(meta, expression->v.DictComp.generators);
        return function_result;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.GeneratorExp.elt) && __tinypy_meta_runtime_comprehensions_validate(meta, expression->v.GeneratorExp.generators);
        return function_result;
    case TINYPY_AST_KIND_YIELD:
        function_result = expression->v.Yield.value == NULL || __tinypy_meta_runtime_expression_validate(meta, expression->v.Yield.value);
        return function_result;
    case TINYPY_AST_KIND_COMPARE:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.Compare.left) && __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.Compare.comparators);
        return function_result;
    case TINYPY_AST_KIND_CALL:
        if (__tinypy_meta_runtime_expression_validate(meta, expression->v.Call.func) == 0 || __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.Call.args) == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords); ++index) {
            if (__tinypy_meta_runtime_expression_validate(meta, ((tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.keywords, index))->value) == 0) {
                return TINYPY_FALSE;
            }
        }
        tinypy_bool_t return_value_1 = (expression->v.Call.starargs == NULL || __tinypy_meta_runtime_expression_validate(meta, expression->v.Call.starargs) != 0) && (expression->v.Call.kwargs == NULL || __tinypy_meta_runtime_expression_validate(meta, expression->v.Call.kwargs) != 0);
        return return_value_1;
    case TINYPY_AST_KIND_REPR:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.Repr.value);
        return function_result;
    case TINYPY_AST_KIND_ATTRIBUTE:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.Attribute.value);
        return function_result;
    case TINYPY_AST_KIND_SUBSCRIPT:
        function_result = __tinypy_meta_runtime_expression_validate(meta, expression->v.Subscript.value) && __tinypy_meta_runtime_slice_validate(meta, expression->v.Subscript.slice);
        return function_result;
    case TINYPY_AST_KIND_LIST:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.List.elts);
        return function_result;
    case TINYPY_AST_KIND_TUPLE:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, expression->v.Tuple.elts);
        return function_result;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_alias_validate(tinypy_meta_context_t *meta, tinypy_ast_alias_t alias, int32_t line, int32_t column) {
    tinypy_ast_identifier_t binding = alias->asname;
    const char *data;
    size_t size;

    if (binding != NULL) {
        tinypy_bool_t return_value_1 = __tinypy_meta_runtime_identifier_validate(meta, binding, line, column);
        return return_value_1;
    }
    data = (const char *)tinypy_string_view(alias->name, &size);
    tinypy_bool_t return_value_2 = size == 4U && memcmp(data, "meta", 4U) == 0 ? __tinypy_meta_fail(meta, "meta cannot be rebound by import", line, column) : 1;
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_runtime_statement_validate(tinypy_meta_context_t *meta, tinypy_ast_statement_t statement) {
    tinypy_bool_t function_result;
    int32_t index;

    switch (statement->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        function_result = __tinypy_meta_runtime_identifier_validate(meta, statement->v.FunctionDef.name, statement->lineno, statement->col_offset) && __tinypy_meta_runtime_arguments_validate(meta, statement->v.FunctionDef.args, statement->lineno, statement->col_offset) && __tinypy_meta_runtime_expression_sequence_validate(meta, statement->v.FunctionDef.decorator_list) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.FunctionDef.body);
        return function_result;
    case TINYPY_AST_KIND_CLASS_DEF:
        function_result = __tinypy_meta_runtime_identifier_validate(meta, statement->v.ClassDef.name, statement->lineno, statement->col_offset) && __tinypy_meta_runtime_expression_sequence_validate(meta, statement->v.ClassDef.bases) && __tinypy_meta_runtime_expression_sequence_validate(meta, statement->v.ClassDef.decorator_list) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.ClassDef.body);
        return function_result;
    case TINYPY_AST_KIND_RETURN:
        function_result = statement->v.Return.value == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Return.value);
        return function_result;
    case TINYPY_AST_KIND_DELETE:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, statement->v.Delete.targets);
        return function_result;
    case TINYPY_AST_KIND_ASSIGN:
        function_result = __tinypy_meta_runtime_expression_sequence_validate(meta, statement->v.Assign.targets) && __tinypy_meta_runtime_expression_validate(meta, statement->v.Assign.value);
        return function_result;
    case TINYPY_AST_KIND_AUG_ASSIGN:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.AugAssign.target) && __tinypy_meta_runtime_expression_validate(meta, statement->v.AugAssign.value);
        return function_result;
    case TINYPY_AST_KIND_PRINT:
        function_result = (statement->v.Print.dest == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Print.dest) != 0) && __tinypy_meta_runtime_expression_sequence_validate(meta, statement->v.Print.values);
        return function_result;
    case TINYPY_AST_KIND_FOR:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.For.target) && __tinypy_meta_runtime_expression_validate(meta, statement->v.For.iter) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.For.body) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.For.orelse);
        return function_result;
    case TINYPY_AST_KIND_WHILE:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.While.test) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.While.body) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.While.orelse);
        return function_result;
    case TINYPY_AST_KIND_IF:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.If.test) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.If.body) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.If.orelse);
        return function_result;
    case TINYPY_AST_KIND_WITH:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.With.context_expr) && (statement->v.With.optional_vars == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.With.optional_vars) != 0) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.With.body);
        return function_result;
    case TINYPY_AST_KIND_RAISE:
        function_result = (statement->v.Raise.type == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Raise.type) != 0) && (statement->v.Raise.inst == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Raise.inst) != 0) && (statement->v.Raise.tback == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Raise.tback) != 0);
        return function_result;
    case TINYPY_AST_KIND_TRY_EXCEPT:
        if (__tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.TryExcept.body) == 0 || __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.TryExcept.orelse) == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.handlers); ++index) {
            tinypy_ast_exception_handler_t handler = (tinypy_ast_exception_handler_t)TINYPY_AST_SEQUENCE_GET(statement->v.TryExcept.handlers, index);
            if ((handler->v.ExceptHandler.type != NULL && __tinypy_meta_runtime_expression_validate(meta, handler->v.ExceptHandler.type) == 0) || (handler->v.ExceptHandler.name != NULL && __tinypy_meta_runtime_expression_validate(meta, handler->v.ExceptHandler.name) == 0) || __tinypy_meta_runtime_statement_sequence_validate(meta, handler->v.ExceptHandler.body) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_TRY_FINALLY:
        function_result = __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.TryFinally.body) && __tinypy_meta_runtime_statement_sequence_validate(meta, statement->v.TryFinally.finalbody);
        return function_result;
    case TINYPY_AST_KIND_ASSERT:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.Assert.test) && (statement->v.Assert.msg == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Assert.msg) != 0);
        return function_result;
    case TINYPY_AST_KIND_IMPORT:
    case TINYPY_AST_KIND_IMPORT_FROM: {
        tinypy_ast_sequence_t *aliases = statement->kind == TINYPY_AST_KIND_IMPORT ? statement->v.Import.names : statement->v.ImportFrom.names;
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(aliases); ++index) {
            if (__tinypy_meta_runtime_alias_validate(meta, (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(aliases, index), statement->lineno, statement->col_offset) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    }
    case TINYPY_AST_KIND_EXEC:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.Exec.body) && (statement->v.Exec.globals == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Exec.globals) != 0) && (statement->v.Exec.locals == NULL || __tinypy_meta_runtime_expression_validate(meta, statement->v.Exec.locals) != 0);
        return function_result;
    case TINYPY_AST_KIND_GLOBAL:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.Global.names); ++index) {
            if (__tinypy_meta_runtime_identifier_validate(meta, (tinypy_ast_identifier_t)TINYPY_AST_SEQUENCE_GET(statement->v.Global.names, index), statement->lineno, statement->col_offset) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_EXPR:
        function_result = __tinypy_meta_runtime_expression_validate(meta, statement->v.Expr.value);
        return function_result;
    case TINYPY_AST_KIND_PASS:
    case TINYPY_AST_KIND_BREAK:
    case TINYPY_AST_KIND_CONTINUE:
        return TINYPY_TRUE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_meta_collect_templates(tinypy_meta_context_t *meta, tinypy_ast_sequence_t *source, tinypy_ast_sequence_t **out_runtime) {
    tinypy_meta_statement_builder_t runtime;
    int32_t index;
    int32_t output_index = 0;

    (void)memset(&runtime, 0, sizeof(runtime));
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(source); ++index) {
        tinypy_ast_statement_t statement = (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(source, index);

        if (__tinypy_meta_is_template(statement) != 0) {
            tinypy_meta_template_t *item;
            if (__tinypy_meta_template_find(meta, statement->v.FunctionDef.name) != NULL) {
                tinypy_bool_t return_value_1 = __tinypy_meta_fail(meta, "duplicate meta template name", statement->lineno, statement->col_offset);
                return return_value_1;
            }
            item = (tinypy_meta_template_t *)tinypy_internal_compiler_arena_allocate(meta->compile, sizeof(*item));
            if (item == NULL) {
                return TINYPY_FALSE;
            }
            item->name = statement->v.FunctionDef.name;
            item->definition = statement;
            item->next = meta->templates;
            meta->templates = item;
        }
        else if (__tinypy_meta_builder_append(meta, &runtime, statement) == 0) {
            return TINYPY_FALSE;
        }
    }
    *out_runtime = TINYPY_AST_SEQUENCE_NEW((int32_t)runtime.size, meta->compile);
    tinypy_meta_statement_node_t *node = runtime.head;
    while (node != NULL) {
        TINYPY_AST_SEQUENCE_SET(*out_runtime, output_index, node->statement);
        output_index += 1;
        node = node->next;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_meta_transform(tinypy_compile_ctx_t *ctx, tinypy_ast_module_t module) {
    tinypy_meta_context_t meta;
    tinypy_ast_sequence_t *runtime;

    (void)memset(&meta, 0, sizeof(meta));
    meta.compile = ctx;
    if (module->kind == TINYPY_AST_KIND_EXPRESSION) {
        tinypy_bool_t return_value_1 = __tinypy_meta_runtime_expression_validate(&meta, module->v.Expression.body);
        return return_value_1;
    }
    if (module->kind == TINYPY_AST_KIND_INTERACTIVE) {
        tinypy_bool_t return_value_2 = __tinypy_meta_runtime_statement_sequence_validate(&meta, module->v.Interactive.body);
        return return_value_2;
    }
    if (module->kind == TINYPY_AST_KIND_SUITE) {
        tinypy_bool_t return_value_3 = __tinypy_meta_runtime_statement_sequence_validate(&meta, module->v.Suite.body);
        return return_value_3;
    }
    if (module->kind != TINYPY_AST_KIND_MODULE) {
        tinypy_bool_t return_value_4 = __tinypy_meta_fail(&meta, "unsupported module form for meta expansion", 1, 0);
        return return_value_4;
    }
    if (__tinypy_meta_collect_templates(&meta, module->v.Module.body, &runtime) == 0) {
        return TINYPY_FALSE;
    }
    module->v.Module.body = __tinypy_meta_expand_sequence(&meta, runtime);
    tinypy_bool_t return_value_5 = module->v.Module.body != NULL && ctx->failed == 0 && __tinypy_meta_runtime_statement_sequence_validate(&meta, module->v.Module.body) != 0;
    return return_value_5;
}
