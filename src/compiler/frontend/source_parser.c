/* tinypy memory-only source-to-CST parser driver. */

#include "value_ops.h"
#include "tokenizer.h"
#include "cst.h"
#include "grammar.h"
#include "parser.h"
#include "source_parser.h"
#include "parser_error.h"
#include "grammar_symbols.h"

//////////////////////////////////////////////////////////////////////////
static void __tinypy_frontend_init_error(tinypy_parser_error_detail_t *error, const char *filename) {
    error->result = TINYPY_PARSER_OK;
    error->filename = filename;
    error->line_number = 0;
    error->offset = 0;
    error->text = NULL;
    error->token = -1;
    error->expected = -1;
}
//////////////////////////////////////////////////////////////////////////
static char *__tinypy_frontend_token_copy(tinypy_compile_ctx_t *ctx, const char *begin, const char *end) {
    size_t size = begin != NULL && end != NULL ? (size_t)(end - begin) : 0U;
    char *copy;

    assert(begin == NULL || end >= begin);
    copy = (char *)tinypy_internal_compiler_arena_allocate(ctx, size + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (size != 0U) {
        (void)memcpy(copy, begin, size);
    }
    copy[size] = '\0';
    return copy;
}
//////////////////////////////////////////////////////////////////////////
tinypy_cst_node_t *tinypy_internal_parse_source(tinypy_compile_ctx_t *ctx, const char *source, size_t source_size, const char *filename, const tinypy_parser_grammar_t *g, int start, tinypy_parser_error_detail_t *error, int *flags) {
    tinypy_cst_node_t *result = NULL;
    int started = 0;

    assert(ctx != NULL);
    assert(source != NULL);
    assert(filename != NULL);
    assert(g != NULL);
    assert(error != NULL);
    assert(flags != NULL);
    __tinypy_frontend_init_error(error, filename);
    tinypy_tokenizer_t *tok = tinypy_internal_tokenizer_from_string(ctx, source, source_size, start == TINYPY_GRAMMAR_FILE_INPUT);
    if (tok == NULL) {
        error->result = TINYPY_PARSER_OUT_OF_MEMORY;
        return NULL;
    }
    tok->filename = filename;
    tok->alterror = 1;
    tinypy_parser_t *parser = tinypy_internal_parser_new(ctx, g, start);
    if (parser == NULL) {
        error->result = TINYPY_PARSER_OUT_OF_MEMORY;
        return NULL;
    }
    if ((*flags & TINYPY_PARSER_FLAG_PRINT_IS_FUNCTION) != 0) {
        parser->flags |= TINYPY_CODE_FUTURE_PRINT_FUNCTION;
    }
    if ((*flags & TINYPY_PARSER_FLAG_UNICODE_LITERALS) != 0) {
        parser->flags |= TINYPY_CODE_FUTURE_UNICODE_LITERALS;
    }

    for (;;) {
        char *begin;
        char *end;
        char *token_text;
        int token_type;
        int column;

        if (ctx->limits.max_tokens != 0U && ctx->token_count >= ctx->limits.max_tokens) {
            error->result = TINYPY_PARSER_OUT_OF_MEMORY;
            break;
        }
        token_type = tinypy_internal_tokenizer_get(tok, &begin, &end);
        ctx->token_count += 1U;
        if (token_type == TINYPY_TOKEN_ERROR) {
            error->result = tok->done;
            break;
        }
        if (token_type == TINYPY_TOKEN_END && started != 0) {
            token_type = TINYPY_TOKEN_NEWLINE;
            started = 0;
            if (tok->indent != 0 && ((*flags & TINYPY_PARSER_FLAG_DONT_IMPLY_DEDENT) == 0)) {
                tok->pendin = -tok->indent;
                tok->indent = 0;
            }
        }
        else {
            started = 1;
        }
        token_text = __tinypy_frontend_token_copy(ctx, begin, end);
        if (token_text == NULL) {
            error->result = TINYPY_PARSER_OUT_OF_MEMORY;
            break;
        }
        column = begin != NULL && begin >= tok->line_start ? (int)(begin - tok->line_start) : -1;
        error->result = tinypy_internal_parser_add_token(parser, token_type, token_text, tok->line_number, column, &error->expected);
        if (error->result != TINYPY_PARSER_OK) {
            if (error->result != TINYPY_PARSER_DONE) {
                error->token = token_type;
            }
            break;
        }
    }

    if (error->result == TINYPY_PARSER_DONE) {
        result = parser->tree;
        parser->tree = NULL;
    }
    *flags = (int)parser->flags;
    if (result == NULL) {
        if (tok->line_number <= 1 && tok->done == TINYPY_PARSER_EOF) {
            error->result = TINYPY_PARSER_EOF;
        }
        error->line_number = tok->line_number;
        error->offset = tok->cur != NULL && tok->line_start != NULL && tok->cur >= tok->line_start ? (int)(tok->cur - tok->line_start) : 0;
    }
    tinypy_internal_parser_release(parser);
    tinypy_internal_tokenizer_release(tok);
    return result;
}
