#include "internal.h"

#include "ast_nodes.h"
#include "value_ops.h"

#include "tinypy/preprocessor.h"
#include "tinypy/numeric.h"
#include "tinypy/representation.h"
#include "tinypy/tuple.h"
#include "tinypy/value.h"

#include "../artifact/sha256.h"

#include <float.h>
#include <limits.h>
#include <string.h>

#define TINYPY_PREPROCESS_RESULT_STATE UINT32_C(0x54505245)
#define TINYPY_RENDER_CHUNK_SIZE ((size_t)4096U)

typedef struct tinypy_render_chunk_t {
    struct tinypy_render_chunk_t *next;
    size_t size;
    uint8_t bytes[TINYPY_RENDER_CHUNK_SIZE];
} tinypy_render_chunk_t;

typedef struct tinypy_render_builder_t {
    tinypy_compile_ctx_t *compile;
    tinypy_render_chunk_t *head;
    tinypy_render_chunk_t *tail;
    size_t size;
    int32_t line;
    int32_t column;
} tinypy_render_builder_t;

struct tinypy_preprocess_result_t {
    uint32_t state;
    tinypy_vm_t *vm;
    size_t allocation_size;
    size_t source_size;
    size_t source_map_size;
    size_t entry_count;
    char *source;
    uint8_t *source_map;
    tinypy_source_map_entry_t *entries;
    uint8_t source_map_digest[TINYPY_SOURCE_MAP_DIGEST_SIZE];
    uint8_t storage[];
};

static tinypy_bool_t __tinypy_render_expression(tinypy_render_builder_t *builder, tinypy_ast_expression_t expression);
static tinypy_bool_t __tinypy_render_statement(tinypy_render_builder_t *builder, tinypy_ast_statement_t statement, size_t indentation);
static tinypy_bool_t __tinypy_render_arguments(tinypy_render_builder_t *builder, tinypy_ast_arguments_t arguments);

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_fail(tinypy_render_builder_t *builder, const char *message, int32_t line, int32_t column) {
    tinypy_internal_compiler_error(builder->compile, TINYPY_ERROR_PREPROCESSOR, message, line, column + 1, builder->compile->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_limit(tinypy_render_builder_t *builder, const char *message, int32_t line, int32_t column) {
    tinypy_internal_compiler_error(builder->compile, TINYPY_ERROR_COMPILER_LIMIT, message, line, column + 1, builder->compile->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_append(tinypy_render_builder_t *builder, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;

    if (size == 0U) {
        return TINYPY_TRUE;
    }
    if (builder->compile->limits.max_generated_source_bytes != 0U && (builder->size > builder->compile->limits.max_generated_source_bytes || size > builder->compile->limits.max_generated_source_bytes - builder->size)) {
        tinypy_bool_t return_value_1 = __tinypy_render_limit(builder, "generated source byte limit exceeded", 1, 0);
        return return_value_1;
    }
    while (size != 0U) {
        size_t available;
        size_t copied;
        size_t index;

        if (builder->tail == NULL || builder->tail->size == TINYPY_RENDER_CHUNK_SIZE) {
            tinypy_render_chunk_t *chunk = (tinypy_render_chunk_t *)tinypy_internal_compiler_arena_allocate(builder->compile, sizeof(*chunk));

            if (chunk == NULL) {
                tinypy_bool_t return_value_2 = __tinypy_render_limit(builder, "expanded output exceeds compiler arena limit", builder->line, builder->column);
                return return_value_2;
            }
            if (builder->tail != NULL) {
                builder->tail->next = chunk;
            }
            else {
                builder->head = chunk;
            }
            builder->tail = chunk;
        }
        available = TINYPY_RENDER_CHUNK_SIZE - builder->tail->size;
        copied = size < available ? size : available;
        (void)memcpy(builder->tail->bytes + builder->tail->size, bytes, copied);
        for (index = 0U; index < copied; ++index) {
            if (bytes[index] == (uint8_t)'\n') {
                builder->line += 1;
                builder->column = 0;
            }
            else {
                builder->column += 1;
            }
        }
        builder->tail->size += copied;
        builder->size += copied;
        bytes += copied;
        size -= copied;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_text(tinypy_render_builder_t *builder, const char *text) {
    unsigned long size = strlen(text);
    tinypy_bool_t return_value_1 = __tinypy_render_append(builder, text, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_character(tinypy_render_builder_t *builder, char character) {
    tinypy_bool_t return_value_1 = __tinypy_render_append(builder, &character, 1U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_identifier(tinypy_render_builder_t *builder, tinypy_ast_identifier_t identifier) {
    size_t size;
    const void *data;

    if (identifier == NULL) {
        return TINYPY_TRUE;
    }
    data = tinypy_string_view(identifier, &size);
    tinypy_bool_t return_value_1 = __tinypy_render_append(builder, data, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_indent(tinypy_render_builder_t *builder, size_t indentation) {
    size_t index;

    for (index = 0U; index < indentation; ++index) {
        if (__tinypy_render_text(builder, "    ") == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_repr_literal(tinypy_render_builder_t *builder, tinypy_value_t *value) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *representation = tinypy_object_repr(value, &error);
    const void *bytes;
    size_t size;

    if (representation == NULL) {
        const char *message = error != NULL ? tinypy_error_message(error, NULL) : "unable to render literal";
        tinypy_internal_exception_clear_raised(builder->compile->vm);
        (void)__tinypy_render_fail(builder, message, 1, 0);
        if (error != NULL) {
            tinypy_error_release(error);
        }
        return TINYPY_FALSE;
    }
    bytes = tinypy_string_view(representation, &size);
    if (__tinypy_render_append(builder, bytes, size) == 0) {
        TINYPY_DECREF(representation);
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(representation);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_hex(tinypy_render_builder_t *builder, uint32_t value, size_t width) {
    static const char digits[] = "0123456789abcdef";
    char output[8];
    size_t index;

    for (index = 0U; index < width; ++index) {
        output[width - index - 1U] = digits[(value >> (index * 4U)) & UINT32_C(0xf)];
    }
    tinypy_bool_t return_value_1 = __tinypy_render_append(builder, output, width);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_escaped_code_point(tinypy_render_builder_t *builder, uint32_t code_point, tinypy_bool_t unicode) {
    if (code_point == (uint32_t)'\\' || code_point == (uint32_t)'\'') {
        tinypy_bool_t return_value = __tinypy_render_character(builder, '\\') && __tinypy_render_character(builder, (char)code_point);
        return return_value;
    }
    if (code_point == (uint32_t)'\n') {
        tinypy_bool_t return_value = __tinypy_render_text(builder, "\\n");
        return return_value;
    }
    if (code_point == (uint32_t)'\r') {
        tinypy_bool_t return_value = __tinypy_render_text(builder, "\\r");
        return return_value;
    }
    if (code_point == (uint32_t)'\t') {
        tinypy_bool_t return_value = __tinypy_render_text(builder, "\\t");
        return return_value;
    }
    if (code_point >= UINT32_C(0x20) && code_point < UINT32_C(0x7f)) {
        tinypy_bool_t return_value_1 = __tinypy_render_character(builder, (char)code_point);
        return return_value_1;
    }
    if (unicode == 0) {
        tinypy_bool_t return_value = __tinypy_render_text(builder, "\\x") && __tinypy_render_hex(builder, code_point, 2U);
        return return_value;
    }
    if (code_point <= UINT32_C(0xffff)) {
        tinypy_bool_t return_value = __tinypy_render_text(builder, "\\u") && __tinypy_render_hex(builder, code_point, 4U);
        return return_value;
    }
    tinypy_bool_t return_value = __tinypy_render_text(builder, "\\U") && __tinypy_render_hex(builder, code_point, 8U);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_text_literal(tinypy_render_builder_t *builder, tinypy_value_t *value) {
    tinypy_bool_t unicode = tinypy_typeof(value) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    const uint8_t *bytes;
    size_t size;
    size_t index = 0U;

    if (unicode != 0) {
        size_t code_points;

        bytes = (const uint8_t *)tinypy_unicode_utf8_view(value, &size, &code_points);
        if (__tinypy_render_character(builder, 'u') == 0) {
            return TINYPY_FALSE;
        }
    }
    else {
        bytes = (const uint8_t *)tinypy_string_view(value, &size);
    }
    if (__tinypy_render_character(builder, '\'') == 0) {
        return TINYPY_FALSE;
    }
    while (index < size) {
        uint32_t code_point = bytes[index++];

        if (unicode != 0 && code_point >= UINT32_C(0x80)) {
            size_t continuation_count;
            size_t continuation;

            if (code_point < UINT32_C(0xe0)) {
                code_point &= UINT32_C(0x1f);
                continuation_count = 1U;
            }
            else if (code_point < UINT32_C(0xf0)) {
                code_point &= UINT32_C(0x0f);
                continuation_count = 2U;
            }
            else {
                code_point &= UINT32_C(0x07);
                continuation_count = 3U;
            }
            for (continuation = 0U; continuation < continuation_count; ++continuation) {
                code_point = (code_point << 6U) | (uint32_t)(bytes[index++] & UINT8_C(0x3f));
            }
        }
        if (__tinypy_render_escaped_code_point(builder, code_point, unicode) == 0) {
            return TINYPY_FALSE;
        }
    }
    tinypy_bool_t return_value = __tinypy_render_character(builder, '\'');
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_double_literal(tinypy_render_builder_t *builder, double value) {
    tinypy_bool_t result;

    if (value != value) {
        tinypy_bool_t return_value_1 = __tinypy_render_text(builder, "(1e400 - 1e400)");
        return return_value_1;
    }
    if (value > DBL_MAX) {
        tinypy_bool_t return_value_2 = __tinypy_render_text(builder, "1e400");
        return return_value_2;
    }
    if (value < -DBL_MAX) {
        tinypy_bool_t return_value_3 = __tinypy_render_text(builder, "-1e400");
        return return_value_3;
    }
    tinypy_value_t *temporary = tinypy_float_from_double(builder->compile->vm, value);
    result = __tinypy_render_repr_literal(builder, temporary);
    TINYPY_DECREF(temporary);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_literal(tinypy_render_builder_t *builder, tinypy_value_t *value) {
    tinypy_value_type_e type = tinypy_typeof(value);

    if (type == TINYPY_VALUE_STRING || type == TINYPY_VALUE_UNICODE) {
        tinypy_bool_t return_value_1 = __tinypy_render_text_literal(builder, value);
        return return_value_1;
    }
    if (type == TINYPY_VALUE_TUPLE) {
        size_t size = TINYPY_TUPLE_SIZE(value);
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(value);
        tinypy_value_t *const *iterator_begin = iterator;
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(value);

        if (__tinypy_render_character(builder, '(') == 0) {
            return TINYPY_FALSE;
        }
        for (; iterator != iterator_end; ++iterator) {
            if (iterator != iterator_begin && __tinypy_render_text(builder, ", ") == 0) {
                return TINYPY_FALSE;
            }
            tinypy_value_t *item = *iterator;
            if (__tinypy_render_literal(builder, item) == 0) {
                return TINYPY_FALSE;
            }
        }
        if (size == 1U && __tinypy_render_character(builder, ',') == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_2 = __tinypy_render_character(builder, ')');
        return return_value_2;
    }
    if (type == TINYPY_VALUE_FLOAT) {
        double float_as_double = tinypy_float_as_double(value);
        tinypy_bool_t return_value_3 = __tinypy_render_double_literal(builder, float_as_double);
        return return_value_3;
    }
    if (type == TINYPY_VALUE_COMPLEX) {
        double real_value;
        double imaginary_value;

        tinypy_complex_as_doubles(value, &real_value, &imaginary_value);
        if (real_value == real_value && real_value <= DBL_MAX && real_value >= -DBL_MAX && imaginary_value == imaginary_value && imaginary_value <= DBL_MAX && imaginary_value >= -DBL_MAX) {
            tinypy_bool_t return_value_4 = __tinypy_render_repr_literal(builder, value);
            return return_value_4;
        }
        tinypy_bool_t return_value_5 = __tinypy_render_character(builder, '(') && __tinypy_render_double_literal(builder, real_value) && __tinypy_render_text(builder, " + (") && __tinypy_render_double_literal(builder, imaginary_value) && __tinypy_render_text(builder, ") * 1j)");
        return return_value_5;
    }
    tinypy_bool_t return_value_6 = __tinypy_render_repr_literal(builder, value);
    return return_value_6;
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_render_binary_operator(tinypy_ast_binary_operator_e operation) {
    switch (operation) {
    case TINYPY_AST_BINARY_ADD:
        return "+";
    case TINYPY_AST_BINARY_SUBTRACT:
        return "-";
    case TINYPY_AST_BINARY_MULTIPLY:
        return "*";
    case TINYPY_AST_BINARY_DIVIDE:
        return "/";
    case TINYPY_AST_BINARY_MODULO:
        return "%";
    case TINYPY_AST_BINARY_POWER:
        return "**";
    case TINYPY_AST_BINARY_LEFT_SHIFT:
        return "<<";
    case TINYPY_AST_BINARY_RIGHT_SHIFT:
        return ">>";
    case TINYPY_AST_BINARY_BIT_OR:
        return "|";
    case TINYPY_AST_BINARY_BIT_XOR:
        return "^";
    case TINYPY_AST_BINARY_BIT_AND:
        return "&";
    case TINYPY_AST_BINARY_FLOOR_DIVIDE:
        return "//";
    default:
        return "?";
    }
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_render_compare_operator(int32_t operation) {
    switch (operation) {
    case TINYPY_AST_COMPARE_EQUAL:
        return "==";
    case TINYPY_AST_COMPARE_NOT_EQUAL:
        return "!=";
    case TINYPY_AST_COMPARE_LESS:
        return "<";
    case TINYPY_AST_COMPARE_LESS_EQUAL:
        return "<=";
    case TINYPY_AST_COMPARE_GREATER:
        return ">";
    case TINYPY_AST_COMPARE_GREATER_EQUAL:
        return ">=";
    case TINYPY_AST_COMPARE_IS:
        return "is";
    case TINYPY_AST_COMPARE_IS_NOT:
        return "is not";
    case TINYPY_AST_COMPARE_IN:
        return "in";
    case TINYPY_AST_COMPARE_NOT_IN:
        return "not in";
    default:
        return "?";
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_expression_sequence(tinypy_render_builder_t *builder, tinypy_ast_sequence_t *sequence, const char *separator) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (index != 0 && __tinypy_render_text(builder, separator) == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(sequence, index)) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_slice(tinypy_render_builder_t *builder, tinypy_ast_slice_t slice) {
    tinypy_bool_t function_result;
    int32_t index;

    switch (slice->kind) {
    case TINYPY_AST_KIND_ELLIPSIS:
        function_result = __tinypy_render_text(builder, "...");
        return function_result;
    case TINYPY_AST_KIND_INDEX:
        function_result = __tinypy_render_expression(builder, slice->v.Index.value);
        return function_result;
    case TINYPY_AST_KIND_SLICE:
        if (slice->v.Slice.lower != NULL && __tinypy_render_expression(builder, slice->v.Slice.lower) == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_character(builder, ':') == 0) {
            return TINYPY_FALSE;
        }
        if (slice->v.Slice.upper != NULL && __tinypy_render_expression(builder, slice->v.Slice.upper) == 0) {
            return TINYPY_FALSE;
        }
        if (slice->v.Slice.step != NULL) {
            if (__tinypy_render_character(builder, ':') == 0 || __tinypy_render_expression(builder, slice->v.Slice.step) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_EXT_SLICE:
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(slice->v.ExtSlice.dims); ++index) {
            if (index != 0 && __tinypy_render_text(builder, ", ") == 0) {
                return TINYPY_FALSE;
            }
            if (__tinypy_render_slice(builder, (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(slice->v.ExtSlice.dims, index)) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_comprehensions(tinypy_render_builder_t *builder, tinypy_ast_sequence_t *generators) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(generators); ++index) {
        tinypy_ast_comprehension_t generator = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, index);
        int32_t if_index;

        if (__tinypy_render_text(builder, " for ") == 0 || __tinypy_render_expression(builder, generator->target) == 0 || __tinypy_render_text(builder, " in ") == 0 || __tinypy_render_expression(builder, generator->iter) == 0) {
            return TINYPY_FALSE;
        }
        for (if_index = 0; if_index < TINYPY_AST_SEQUENCE_LENGTH(generator->ifs); ++if_index) {
            if (__tinypy_render_text(builder, " if ") == 0 || __tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(generator->ifs, if_index)) == 0) {
                return TINYPY_FALSE;
            }
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_call(tinypy_render_builder_t *builder, tinypy_ast_expression_t expression) {
    int32_t emitted = 0;
    int32_t index;

    if (__tinypy_render_expression(builder, expression->v.Call.func) == 0 || __tinypy_render_character(builder, '(') == 0) {
        return TINYPY_FALSE;
    }
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.args); ++index) {
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.args, index)) == 0) {
            return TINYPY_FALSE;
        }
        emitted = 1;
    }
    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Call.keywords); ++index) {
        tinypy_ast_keyword_t keyword = (tinypy_ast_keyword_t)TINYPY_AST_SEQUENCE_GET(expression->v.Call.keywords, index);
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_identifier(builder, keyword->arg) == 0 || __tinypy_render_character(builder, '=') == 0 || __tinypy_render_expression(builder, keyword->value) == 0) {
            return TINYPY_FALSE;
        }
        emitted = 1;
    }
    if (expression->v.Call.starargs != NULL) {
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_character(builder, '*') == 0 || __tinypy_render_expression(builder, expression->v.Call.starargs) == 0) {
            return TINYPY_FALSE;
        }
        emitted = 1;
    }
    if (expression->v.Call.kwargs != NULL) {
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_text(builder, "**") == 0 || __tinypy_render_expression(builder, expression->v.Call.kwargs) == 0) {
            return TINYPY_FALSE;
        }
    }
    tinypy_bool_t return_value_1 = __tinypy_render_character(builder, ')');
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_expression(tinypy_render_builder_t *builder, tinypy_ast_expression_t expression) {
    tinypy_bool_t function_result;
    int32_t index;

    if (expression == NULL) {
        return TINYPY_TRUE;
    }
    switch (expression->kind) {
    case TINYPY_AST_KIND_NUM:
        function_result = __tinypy_render_literal(builder, expression->v.Num.n);
        return function_result;
    case TINYPY_AST_KIND_STR:
        function_result = __tinypy_render_literal(builder, expression->v.Str.s);
        return function_result;
    case TINYPY_AST_KIND_NAME:
        function_result = __tinypy_render_identifier(builder, expression->v.Name.id);
        return function_result;
    case TINYPY_AST_KIND_ATTRIBUTE:
        function_result = __tinypy_render_character(builder, '(') && __tinypy_render_expression(builder, expression->v.Attribute.value) && __tinypy_render_text(builder, ").") && __tinypy_render_identifier(builder, expression->v.Attribute.attr);
        return function_result;
    case TINYPY_AST_KIND_SUBSCRIPT:
        function_result = __tinypy_render_character(builder, '(') && __tinypy_render_expression(builder, expression->v.Subscript.value) && __tinypy_render_text(builder, ")[") && __tinypy_render_slice(builder, expression->v.Subscript.slice) && __tinypy_render_character(builder, ']');
        return function_result;
    case TINYPY_AST_KIND_BOOL_OP:
        if (__tinypy_render_character(builder, '(') == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.BoolOp.values); ++index) {
            if (index != 0 && __tinypy_render_text(builder, expression->v.BoolOp.op == TINYPY_AST_BOOLEAN_AND ? " and " : " or ") == 0) {
                return TINYPY_FALSE;
            }
            if (__tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.BoolOp.values, index)) == 0) {
                return TINYPY_FALSE;
            }
        }
        tinypy_bool_t return_value_1 = __tinypy_render_character(builder, ')');
        return return_value_1;
    case TINYPY_AST_KIND_BIN_OP: {
        const char *operator_text = __tinypy_render_binary_operator(expression->v.BinOp.op);
        tinypy_bool_t return_value_2 = __tinypy_render_character(builder, '(') && __tinypy_render_expression(builder, expression->v.BinOp.left) && __tinypy_render_character(builder, ' ') && __tinypy_render_text(builder, operator_text) && __tinypy_render_character(builder, ' ') && __tinypy_render_expression(builder, expression->v.BinOp.right) && __tinypy_render_character(builder, ')');
        return return_value_2;
    }
    case TINYPY_AST_KIND_UNARY_OP:
        if (__tinypy_render_character(builder, '(') == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_text(builder, expression->v.UnaryOp.op == TINYPY_AST_UNARY_NOT ? "not " : (expression->v.UnaryOp.op == TINYPY_AST_UNARY_INVERT ? "~" : (expression->v.UnaryOp.op == TINYPY_AST_UNARY_ADD ? "+" : "-"))) == 0 || __tinypy_render_expression(builder, expression->v.UnaryOp.operand) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_3 = __tinypy_render_character(builder, ')');
        return return_value_3;
    case TINYPY_AST_KIND_LAMBDA:
        if (__tinypy_render_text(builder, "(lambda ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_arguments(builder, expression->v.Lambda.args) == 0 || __tinypy_render_text(builder, ": ") == 0 || __tinypy_render_expression(builder, expression->v.Lambda.body) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_4 = __tinypy_render_character(builder, ')');
        return return_value_4;
    case TINYPY_AST_KIND_IF_EXP:
        function_result = __tinypy_render_character(builder, '(') && __tinypy_render_expression(builder, expression->v.IfExp.body) && __tinypy_render_text(builder, " if ") && __tinypy_render_expression(builder, expression->v.IfExp.test) && __tinypy_render_text(builder, " else ") && __tinypy_render_expression(builder, expression->v.IfExp.orelse) && __tinypy_render_character(builder, ')');
        return function_result;
    case TINYPY_AST_KIND_DICT:
        if (__tinypy_render_character(builder, '{') == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(expression->v.Dict.keys); ++index) {
            if (index != 0 && __tinypy_render_text(builder, ", ") == 0) {
                return TINYPY_FALSE;
            }
            if (__tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Dict.keys, index)) == 0 || __tinypy_render_text(builder, ": ") == 0 || __tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Dict.values, index)) == 0) {
                return TINYPY_FALSE;
            }
        }
        tinypy_bool_t return_value_5 = __tinypy_render_character(builder, '}');
        return return_value_5;
    case TINYPY_AST_KIND_SET:
        function_result = __tinypy_render_character(builder, '{') && __tinypy_render_expression_sequence(builder, expression->v.Set.elts, ", ") && __tinypy_render_character(builder, '}');
        return function_result;
    case TINYPY_AST_KIND_LIST_COMP:
        function_result = __tinypy_render_character(builder, '[') && __tinypy_render_expression(builder, expression->v.ListComp.elt) && __tinypy_render_comprehensions(builder, expression->v.ListComp.generators) && __tinypy_render_character(builder, ']');
        return function_result;
    case TINYPY_AST_KIND_SET_COMP:
        function_result = __tinypy_render_character(builder, '{') && __tinypy_render_expression(builder, expression->v.SetComp.elt) && __tinypy_render_comprehensions(builder, expression->v.SetComp.generators) && __tinypy_render_character(builder, '}');
        return function_result;
    case TINYPY_AST_KIND_DICT_COMP:
        function_result = __tinypy_render_character(builder, '{') && __tinypy_render_expression(builder, expression->v.DictComp.key) && __tinypy_render_text(builder, ": ") && __tinypy_render_expression(builder, expression->v.DictComp.value) && __tinypy_render_comprehensions(builder, expression->v.DictComp.generators) && __tinypy_render_character(builder, '}');
        return function_result;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        function_result = __tinypy_render_character(builder, '(') && __tinypy_render_expression(builder, expression->v.GeneratorExp.elt) && __tinypy_render_comprehensions(builder, expression->v.GeneratorExp.generators) && __tinypy_render_character(builder, ')');
        return function_result;
    case TINYPY_AST_KIND_YIELD:
        function_result = __tinypy_render_text(builder, "(yield") && (expression->v.Yield.value == NULL || (__tinypy_render_character(builder, ' ') && __tinypy_render_expression(builder, expression->v.Yield.value))) && __tinypy_render_character(builder, ')');
        return function_result;
    case TINYPY_AST_KIND_COMPARE:
        if (__tinypy_render_character(builder, '(') == 0 || __tinypy_render_expression(builder, expression->v.Compare.left) == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < expression->v.Compare.ops->size; ++index) {
            const char *operator_text = __tinypy_render_compare_operator(expression->v.Compare.ops->elements[index]);
            tinypy_bool_t condition_2 = __tinypy_render_character(builder, ' ') == 0 || __tinypy_render_text(builder, operator_text) == 0;
            if (condition_2 == 0) {
                condition_2 = __tinypy_render_character(builder, ' ') == 0;
            }
            tinypy_bool_t condition = condition_2;
            if (condition == 0) {
                condition = __tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(expression->v.Compare.comparators, index)) == 0;
            }
            if (condition) {
                return TINYPY_FALSE;
            }
        }
        tinypy_bool_t return_value_6 = __tinypy_render_character(builder, ')');
        return return_value_6;
    case TINYPY_AST_KIND_CALL:
        function_result = __tinypy_render_call(builder, expression);
        return function_result;
    case TINYPY_AST_KIND_REPR:
        function_result = __tinypy_render_character(builder, '`') && __tinypy_render_expression(builder, expression->v.Repr.value) && __tinypy_render_character(builder, '`');
        return function_result;
    case TINYPY_AST_KIND_LIST:
        function_result = __tinypy_render_character(builder, '[') && __tinypy_render_expression_sequence(builder, expression->v.List.elts, ", ") && __tinypy_render_character(builder, ']');
        return function_result;
    case TINYPY_AST_KIND_TUPLE:
        if (__tinypy_render_character(builder, '(') == 0 || __tinypy_render_expression_sequence(builder, expression->v.Tuple.elts, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (TINYPY_AST_SEQUENCE_LENGTH(expression->v.Tuple.elts) == 1 && __tinypy_render_character(builder, ',') == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_7 = __tinypy_render_character(builder, ')');
        return return_value_7;
    default:
        function_result = __tinypy_render_fail(builder, "unsupported AST expression in expanded source", expression->lineno, expression->col_offset);
        return function_result;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_arguments(tinypy_render_builder_t *builder, tinypy_ast_arguments_t arguments) {
    int32_t parameter_count = TINYPY_AST_SEQUENCE_LENGTH(arguments->args);
    int32_t default_count = TINYPY_AST_SEQUENCE_LENGTH(arguments->defaults);
    int32_t emitted = 0;
    int32_t index;

    for (index = 0; index < parameter_count; ++index) {
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(arguments->args, index)) == 0) {
            return TINYPY_FALSE;
        }
        if (index >= parameter_count - default_count) {
            if (__tinypy_render_character(builder, '=') == 0 || __tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(arguments->defaults, index - (parameter_count - default_count))) == 0) {
                return TINYPY_FALSE;
            }
        }
        emitted = 1;
    }
    if (arguments->vararg != NULL) {
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_character(builder, '*') == 0 || __tinypy_render_identifier(builder, arguments->vararg) == 0) {
            return TINYPY_FALSE;
        }
        emitted = 1;
    }
    if (arguments->kwarg != NULL) {
        if (emitted != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_text(builder, "**") == 0 || __tinypy_render_identifier(builder, arguments->kwarg) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_source_map_record_t *__tinypy_render_source_map_record(tinypy_render_builder_t *builder, tinypy_ast_statement_t statement) {
    tinypy_source_map_record_t *record = builder->compile->source_map_records;

    while (record != NULL) {
        if (record->statement == statement) {
            return record;
        }
        record = record->next;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_statement_sequence(tinypy_render_builder_t *builder, tinypy_ast_sequence_t *sequence, size_t indentation) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(sequence); ++index) {
        if (__tinypy_render_statement(builder, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(sequence, index), indentation) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_decorators(tinypy_render_builder_t *builder, tinypy_ast_sequence_t *decorators, size_t indentation) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(decorators); ++index) {
        if (__tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_character(builder, '@') == 0 || __tinypy_render_expression(builder, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(decorators, index)) == 0 || __tinypy_render_character(builder, '\n') == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_aliases(tinypy_render_builder_t *builder, tinypy_ast_sequence_t *aliases) {
    int32_t index;

    for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(aliases); ++index) {
        tinypy_ast_alias_t alias = (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(aliases, index);
        if (index != 0 && __tinypy_render_text(builder, ", ") == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_identifier(builder, alias->name) == 0) {
            return TINYPY_FALSE;
        }
        if (alias->asname != NULL && (__tinypy_render_text(builder, " as ") == 0 || __tinypy_render_identifier(builder, alias->asname) == 0)) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_statement(tinypy_render_builder_t *builder, tinypy_ast_statement_t statement, size_t indentation) {
    tinypy_bool_t function_result;
    tinypy_source_map_record_t *record = __tinypy_render_source_map_record(builder, statement);
    int32_t index;

    if (record != NULL) {
        record->generated_line = builder->line;
        record->generated_column = (int32_t)(indentation * 4U);
    }
    if ((statement->kind == TINYPY_AST_KIND_FUNCTION_DEF && __tinypy_render_decorators(builder, statement->v.FunctionDef.decorator_list, indentation) == 0) || (statement->kind == TINYPY_AST_KIND_CLASS_DEF && __tinypy_render_decorators(builder, statement->v.ClassDef.decorator_list, indentation) == 0)) {
        return TINYPY_FALSE;
    }
    if (__tinypy_render_indent(builder, indentation) == 0) {
        return TINYPY_FALSE;
    }
    switch (statement->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        if (__tinypy_render_text(builder, "def ") == 0 || __tinypy_render_identifier(builder, statement->v.FunctionDef.name) == 0 || __tinypy_render_character(builder, '(') == 0 || __tinypy_render_arguments(builder, statement->v.FunctionDef.args) == 0 || __tinypy_render_text(builder, "):\n") == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_1 = __tinypy_render_statement_sequence(builder, statement->v.FunctionDef.body, indentation + 1U);
        return return_value_1;
    case TINYPY_AST_KIND_CLASS_DEF:
        if (__tinypy_render_text(builder, "class ") == 0 || __tinypy_render_identifier(builder, statement->v.ClassDef.name) == 0) {
            return TINYPY_FALSE;
        }
        if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.ClassDef.bases) != 0 && (__tinypy_render_character(builder, '(') == 0 || __tinypy_render_expression_sequence(builder, statement->v.ClassDef.bases, ", ") == 0 || __tinypy_render_character(builder, ')') == 0)) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_text(builder, ":\n") == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_2 = __tinypy_render_statement_sequence(builder, statement->v.ClassDef.body, indentation + 1U);
        return return_value_2;
    case TINYPY_AST_KIND_RETURN:
        if (__tinypy_render_text(builder, "return") == 0 || (statement->v.Return.value != NULL && (__tinypy_render_character(builder, ' ') == 0 || __tinypy_render_expression(builder, statement->v.Return.value) == 0))) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_DELETE:
        if (__tinypy_render_text(builder, "del ") == 0 || __tinypy_render_expression_sequence(builder, statement->v.Delete.targets, ", ") == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_ASSIGN:
        if (__tinypy_render_expression_sequence(builder, statement->v.Assign.targets, " = ") == 0 || __tinypy_render_text(builder, " = ") == 0 || __tinypy_render_expression(builder, statement->v.Assign.value) == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_AUG_ASSIGN: {
        const char *operator_text = __tinypy_render_binary_operator(statement->v.AugAssign.op);
        if (__tinypy_render_expression(builder, statement->v.AugAssign.target) == 0 || __tinypy_render_character(builder, ' ') == 0 || __tinypy_render_text(builder, operator_text) == 0 || __tinypy_render_text(builder, "= ") == 0 || __tinypy_render_expression(builder, statement->v.AugAssign.value) == 0) {
            return TINYPY_FALSE;
        }
        break;
    }
    case TINYPY_AST_KIND_PRINT:
        if (__tinypy_render_text(builder, "print") == 0) {
            return TINYPY_FALSE;
        }
        if (statement->v.Print.dest != NULL && (__tinypy_render_text(builder, " >>") == 0 || __tinypy_render_expression(builder, statement->v.Print.dest) == 0 || (TINYPY_AST_SEQUENCE_LENGTH(statement->v.Print.values) != 0 && __tinypy_render_text(builder, ", ") == 0))) {
            return TINYPY_FALSE;
        }
        else if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.Print.values) != 0 && __tinypy_render_character(builder, ' ') == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_render_expression_sequence(builder, statement->v.Print.values, ", ") == 0 || (statement->v.Print.nl == TINYPY_COMPILER_FALSE && __tinypy_render_character(builder, ',') == 0)) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_FOR:
        if (__tinypy_render_text(builder, "for ") == 0 || __tinypy_render_expression(builder, statement->v.For.target) == 0 || __tinypy_render_text(builder, " in ") == 0 || __tinypy_render_expression(builder, statement->v.For.iter) == 0 || __tinypy_render_text(builder, ":\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.For.body, indentation + 1U) == 0) {
            return TINYPY_FALSE;
        }
        if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.For.orelse) != 0) {
            if (__tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_text(builder, "else:\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.For.orelse, indentation + 1U) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_WHILE:
        if (__tinypy_render_text(builder, "while ") == 0 || __tinypy_render_expression(builder, statement->v.While.test) == 0 || __tinypy_render_text(builder, ":\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.While.body, indentation + 1U) == 0) {
            return TINYPY_FALSE;
        }
        if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.While.orelse) != 0) {
            if (__tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_text(builder, "else:\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.While.orelse, indentation + 1U) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_IF:
        if (__tinypy_render_text(builder, "if ") == 0 || __tinypy_render_expression(builder, statement->v.If.test) == 0 || __tinypy_render_text(builder, ":\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.If.body, indentation + 1U) == 0) {
            return TINYPY_FALSE;
        }
        if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.If.orelse) != 0) {
            if (__tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_text(builder, "else:\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.If.orelse, indentation + 1U) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_WITH:
        if (__tinypy_render_text(builder, "with ") == 0 || __tinypy_render_expression(builder, statement->v.With.context_expr) == 0 || (statement->v.With.optional_vars != NULL && (__tinypy_render_text(builder, " as ") == 0 || __tinypy_render_expression(builder, statement->v.With.optional_vars) == 0)) || __tinypy_render_text(builder, ":\n") == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_3 = __tinypy_render_statement_sequence(builder, statement->v.With.body, indentation + 1U);
        return return_value_3;
    case TINYPY_AST_KIND_RAISE:
        if (__tinypy_render_text(builder, "raise") == 0) {
            return TINYPY_FALSE;
        }
        if (statement->v.Raise.type != NULL && (__tinypy_render_character(builder, ' ') == 0 || __tinypy_render_expression(builder, statement->v.Raise.type) == 0 || (statement->v.Raise.inst != NULL && (__tinypy_render_text(builder, ", ") == 0 || __tinypy_render_expression(builder, statement->v.Raise.inst) == 0)) || (statement->v.Raise.tback != NULL && (__tinypy_render_text(builder, ", ") == 0 || __tinypy_render_expression(builder, statement->v.Raise.tback) == 0)))) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_TRY_EXCEPT:
        if (__tinypy_render_text(builder, "try:\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.TryExcept.body, indentation + 1U) == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.handlers); ++index) {
            tinypy_ast_exception_handler_t handler = (tinypy_ast_exception_handler_t)TINYPY_AST_SEQUENCE_GET(statement->v.TryExcept.handlers, index);
            if (__tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_text(builder, "except") == 0 || (handler->v.ExceptHandler.type != NULL && (__tinypy_render_character(builder, ' ') == 0 || __tinypy_render_expression(builder, handler->v.ExceptHandler.type) == 0)) || (handler->v.ExceptHandler.name != NULL && (__tinypy_render_text(builder, " as ") == 0 || __tinypy_render_expression(builder, handler->v.ExceptHandler.name) == 0)) || __tinypy_render_text(builder, ":\n") == 0 || __tinypy_render_statement_sequence(builder, handler->v.ExceptHandler.body, indentation + 1U) == 0) {
                return TINYPY_FALSE;
            }
        }
        if (TINYPY_AST_SEQUENCE_LENGTH(statement->v.TryExcept.orelse) != 0 && (__tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_text(builder, "else:\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.TryExcept.orelse, indentation + 1U) == 0)) {
            return TINYPY_FALSE;
        }
        return TINYPY_TRUE;
    case TINYPY_AST_KIND_TRY_FINALLY:
        if (__tinypy_render_text(builder, "try:\n") == 0 || __tinypy_render_statement_sequence(builder, statement->v.TryFinally.body, indentation + 1U) == 0 || __tinypy_render_indent(builder, indentation) == 0 || __tinypy_render_text(builder, "finally:\n") == 0) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t return_value_4 = __tinypy_render_statement_sequence(builder, statement->v.TryFinally.finalbody, indentation + 1U);
        return return_value_4;
    case TINYPY_AST_KIND_ASSERT:
        if (__tinypy_render_text(builder, "assert ") == 0 || __tinypy_render_expression(builder, statement->v.Assert.test) == 0 || (statement->v.Assert.msg != NULL && (__tinypy_render_text(builder, ", ") == 0 || __tinypy_render_expression(builder, statement->v.Assert.msg) == 0))) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_IMPORT:
        if (__tinypy_render_text(builder, "import ") == 0 || __tinypy_render_aliases(builder, statement->v.Import.names) == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_IMPORT_FROM:
        if (__tinypy_render_text(builder, "from ") == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < statement->v.ImportFrom.level; ++index) {
            if (__tinypy_render_character(builder, '.') == 0) {
                return TINYPY_FALSE;
            }
        }
        if (__tinypy_render_identifier(builder, statement->v.ImportFrom.module) == 0 || __tinypy_render_text(builder, " import ") == 0 || __tinypy_render_aliases(builder, statement->v.ImportFrom.names) == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_EXEC:
        if (__tinypy_render_text(builder, "exec ") == 0 || __tinypy_render_expression(builder, statement->v.Exec.body) == 0) {
            return TINYPY_FALSE;
        }
        if (statement->v.Exec.globals != NULL) {
            if (__tinypy_render_text(builder, " in ") == 0 || __tinypy_render_expression(builder, statement->v.Exec.globals) == 0) {
                return TINYPY_FALSE;
            }
            if (statement->v.Exec.locals != NULL && (__tinypy_render_text(builder, ", ") == 0 || __tinypy_render_expression(builder, statement->v.Exec.locals) == 0)) {
                return TINYPY_FALSE;
            }
        }
        break;
    case TINYPY_AST_KIND_GLOBAL:
        if (__tinypy_render_text(builder, "global ") == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0; index < TINYPY_AST_SEQUENCE_LENGTH(statement->v.Global.names); ++index) {
            if (index != 0 && __tinypy_render_text(builder, ", ") == 0) {
                return TINYPY_FALSE;
            }
            if (__tinypy_render_identifier(builder, (tinypy_ast_identifier_t)TINYPY_AST_SEQUENCE_GET(statement->v.Global.names, index)) == 0) {
                return TINYPY_FALSE;
            }
        }
        break;
    case TINYPY_AST_KIND_EXPR:
        if (__tinypy_render_expression(builder, statement->v.Expr.value) == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_PASS:
        if (__tinypy_render_text(builder, "pass") == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_BREAK:
        if (__tinypy_render_text(builder, "break") == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_CONTINUE:
        if (__tinypy_render_text(builder, "continue") == 0) {
            return TINYPY_FALSE;
        }
        break;
    default:
        function_result = __tinypy_render_fail(builder, "unsupported AST statement in expanded source", statement->lineno, statement->col_offset);
        return function_result;
    }
    tinypy_bool_t return_value = __tinypy_render_character(builder, '\n');
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_render_flatten(const tinypy_render_builder_t *builder, uint8_t *output) {
    tinypy_render_chunk_t *chunk = builder->head;
    size_t offset = 0U;

    while (chunk != NULL) {
        if (chunk->size != 0U) {
            (void)memcpy(output + offset, chunk->bytes, chunk->size);
        }
        offset += chunk->size;
        chunk = chunk->next;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_render_decimal(tinypy_render_builder_t *builder, int32_t value) {
    char reverse[16];
    char output[16];
    uint32_t magnitude = value < 0 ? (uint32_t)(-(value + 1)) + 1U : (uint32_t)value;
    size_t count = 0U;
    size_t index = 0U;

    if (value < 0) {
        output[index++] = '-';
    }
    do {
        reverse[count++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    while (count != 0U) {
        output[index++] = reverse[--count];
    }
    tinypy_bool_t return_value_1 = __tinypy_render_append(builder, output, index);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_render_record_compare(const tinypy_source_map_record_t *left, const tinypy_source_map_record_t *right) {
    if (left->generated_line != right->generated_line) {
        return left->generated_line < right->generated_line ? -1 : 1;
    }
    if (left->generated_column != right->generated_column) {
        return left->generated_column < right->generated_column ? -1 : 1;
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_source_map_record_t **__tinypy_render_sorted_records(tinypy_compile_ctx_t *ctx) {
    tinypy_source_map_record_t *record;
    size_t count = 0U;
    size_t index;

    if (ctx->source_map_entries == 0U) {
        return NULL;
    }
    tinypy_source_map_record_t **records = (tinypy_source_map_record_t **)tinypy_internal_compiler_arena_allocate(ctx, ctx->source_map_entries * sizeof(*records));
    if (records == NULL) {
        return NULL;
    }
    for (record = ctx->source_map_records; record != NULL; record = record->next) {
        records[count++] = record;
    }
    for (index = 1U; index < count; ++index) {
        tinypy_source_map_record_t *item = records[index];
        size_t position = index;
        while (position != 0U && __tinypy_render_record_compare(records[position - 1U], item) > 0) {
            records[position] = records[position - 1U];
            position -= 1U;
        }
        records[position] = item;
    }
    return records;
}
//////////////////////////////////////////////////////////////////////////
tinypy_preprocess_result_t *tinypy_internal_preprocessor_render(tinypy_compile_ctx_t *ctx, tinypy_ast_module_t module) {
    tinypy_render_builder_t source_builder;
    tinypy_render_builder_t map_builder;
    size_t symbol_bytes = 0U;
    size_t allocation_size;
    size_t index;
    uint8_t *cursor;

    (void)memset(&source_builder, 0, sizeof(source_builder));
    source_builder.compile = ctx;
    source_builder.line = 1;
    if (module->kind == TINYPY_AST_KIND_MODULE) {
        if (__tinypy_render_statement_sequence(&source_builder, module->v.Module.body, 0U) == 0) {
            return NULL;
        }
    }
    else if (module->kind == TINYPY_AST_KIND_INTERACTIVE) {
        if (__tinypy_render_statement_sequence(&source_builder, module->v.Interactive.body, 0U) == 0) {
            return NULL;
        }
    }
    else if (module->kind == TINYPY_AST_KIND_SUITE) {
        if (__tinypy_render_statement_sequence(&source_builder, module->v.Suite.body, 0U) == 0) {
            return NULL;
        }
    }
    else if (module->kind == TINYPY_AST_KIND_EXPRESSION) {
        if (__tinypy_render_expression(&source_builder, module->v.Expression.body) == 0 || __tinypy_render_character(&source_builder, '\n') == 0) {
            return NULL;
        }
    }
    else {
        return NULL;
    }
    tinypy_source_map_record_t **records = __tinypy_render_sorted_records(ctx);
    if (ctx->source_map_entries != 0U && records == NULL) {
        (void)__tinypy_render_limit(&source_builder, "source map exceeds compiler arena limit", 1, 0);
        return NULL;
    }
    (void)memset(&map_builder, 0, sizeof(map_builder));
    map_builder.compile = ctx;
    map_builder.line = 1;
    if (__tinypy_render_text(&map_builder, "tinypy-source-map-v1\n") == 0) {
        return NULL;
    }
    for (index = 0U; index < ctx->source_map_entries; ++index) {
        size_t symbol_size;
        const void *symbol = tinypy_string_view(records[index]->symbol, &symbol_size);
        if (__tinypy_render_decimal(&map_builder, records[index]->generated_line) == 0 || __tinypy_render_character(&map_builder, '\t') == 0 || __tinypy_render_decimal(&map_builder, records[index]->generated_column) == 0 || __tinypy_render_character(&map_builder, '\t') == 0 || __tinypy_render_decimal(&map_builder, records[index]->template_line) == 0 || __tinypy_render_character(&map_builder, '\t') == 0 || __tinypy_render_decimal(&map_builder, records[index]->template_column) == 0 || __tinypy_render_character(&map_builder, '\t') == 0 || __tinypy_render_decimal(&map_builder, records[index]->expansion_line) == 0 || __tinypy_render_character(&map_builder, '\t') == 0 || __tinypy_render_decimal(&map_builder, records[index]->expansion_column) == 0 || __tinypy_render_character(&map_builder, '\t') == 0 || __tinypy_render_append(&map_builder, symbol, symbol_size) == 0 || __tinypy_render_character(&map_builder, '\n') == 0) {
            return NULL;
        }
        symbol_bytes += symbol_size;
    }
    allocation_size = sizeof(tinypy_preprocess_result_t) + ctx->source_map_entries * sizeof(tinypy_source_map_entry_t);
    allocation_size += source_builder.size + 1U;
    allocation_size += map_builder.size + 1U;
    allocation_size += symbol_bytes;
    tinypy_preprocess_result_t *result = (tinypy_preprocess_result_t *)tinypy_internal_vm_allocate(ctx->vm, allocation_size);
    (void)memset(result, 0, sizeof(*result));
    result->state = TINYPY_PREPROCESS_RESULT_STATE;
    result->vm = ctx->vm;
    result->allocation_size = allocation_size;
    result->source_size = source_builder.size;
    result->source_map_size = map_builder.size;
    result->entry_count = ctx->source_map_entries;
    cursor = result->storage;
    result->entries = (tinypy_source_map_entry_t *)cursor;
    cursor += ctx->source_map_entries * sizeof(*result->entries);
    result->source = (char *)cursor;
    __tinypy_render_flatten(&source_builder, cursor);
    cursor[source_builder.size] = 0U;
    cursor += source_builder.size + 1U;
    result->source_map = cursor;
    __tinypy_render_flatten(&map_builder, cursor);
    cursor[map_builder.size] = 0U;
    cursor += map_builder.size + 1U;
    for (index = 0U; index < ctx->source_map_entries; ++index) {
        tinypy_source_map_entry_t *entry = &result->entries[index];
        size_t symbol_size;
        const void *symbol = tinypy_string_view(records[index]->symbol, &symbol_size);
        (void)memset(entry, 0, sizeof(*entry));
        entry->abi_version = TINYPY_PREPROCESSOR_ABI_VERSION;
        entry->struct_size = (uint32_t)sizeof(*entry);
        entry->generated_line = records[index]->generated_line;
        entry->generated_column = records[index]->generated_column;
        entry->template_line = records[index]->template_line;
        entry->template_column = records[index]->template_column;
        entry->expansion_line = records[index]->expansion_line;
        entry->expansion_column = records[index]->expansion_column;
        entry->generated_symbol = (const char *)cursor;
        entry->generated_symbol_size = symbol_size;
        if (symbol_size != 0U) {
            (void)memcpy(cursor, symbol, symbol_size);
        }
        cursor += symbol_size;
    }
    tinypy_sha256_digest(result->source_map, result->source_map_size, result->source_map_digest);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_preprocess_result_destroy(tinypy_preprocess_result_t *result) {
    size_t allocation_size;

    tinypy_vm_t *vm = result->vm;
    allocation_size = result->allocation_size;
    result->state = 0U;
    tinypy_internal_vm_deallocate(vm, result, allocation_size);
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_preprocess_result_expanded_source(const tinypy_preprocess_result_t *result, size_t *out_size) {
    if (out_size != NULL) {
        *out_size = result->source_size;
    }
    return result->source;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_preprocess_result_source_map(const tinypy_preprocess_result_t *result, size_t *out_size) {
    if (out_size != NULL) {
        *out_size = result->source_map_size;
    }
    return result->source_map;
}
//////////////////////////////////////////////////////////////////////////
const uint8_t *tinypy_preprocess_result_source_map_digest(const tinypy_preprocess_result_t *result) {
    return result->source_map_digest;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_preprocess_result_source_map_count(const tinypy_preprocess_result_t *result) {
    return result->entry_count;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_preprocess_result_source_map_at(const tinypy_preprocess_result_t *result, size_t index, tinypy_source_map_entry_t *out_entry) {
    *out_entry = result->entries[index];
}
