/* tinypy memory-only source-to-CST parser driver. */

#include "value_ops.h"
#include "tokenizer.h"
#include "cst.h"
#include "grammar.h"
#include "parser.h"
#include "source_parser.h"
#include "parser_error.h"
#include "grammar_symbols.h"

typedef struct tinypy_source_position_t {
    char *buf;
    char *cur;
    char *inp;
    int32_t line_number;
    int32_t done;
    tinypy_bool_t phantom_line;
} tinypy_source_position_t;

typedef struct tinypy_source_token_t {
    int32_t type;
    int32_t line;
    int32_t column;
    char *text;
    tinypy_source_position_t position;
} tinypy_source_token_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_frontend_init_error(tinypy_parser_error_detail_t *error, const char *filename) {
    error->result = TINYPY_PARSER_OK;
    error->filename = filename;
    error->line_number = 0;
    error->offset = 0;
    error->text = NULL;
    error->text_size = 0U;
    error->message = NULL;
    error->token = -1;
    error->expected = -1;
}
//////////////////////////////////////////////////////////////////////////
static char *__tinypy_frontend_token_copy(tinypy_compile_ctx_t *ctx, const char *begin, const char *end) {
    size_t size = begin != NULL && end != NULL ? (size_t)(end - begin) : 0U;
    char *copy;

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
/* parsetok reports the tokenizer buffer of the failing logical line and the
   offset of the scan position in it; a line that failed to decode has
   neither, and the end of a file reads as an empty line. */
static void __tinypy_frontend_error_location(tinypy_tokenizer_t *tok, tinypy_parser_error_detail_t *error) {
    if (tok->line_number <= 1 && tok->done == TINYPY_PARSER_EOF) {
        error->result = TINYPY_PARSER_EOF;
    }
    error->line_number = tok->line_number;
    if (error->result == TINYPY_PARSER_DECODE_ERROR) {
        error->message = tok->error_message;
        return;
    }
    if (tok->buf == NULL || tok->cur == NULL || tok->cur < tok->buf) {
        return;
    }
    /* At the end of a file the file tokenizer reads only a terminator into
       its buffer, over the first byte of the last line it held: the quoted
       text is empty, while the offset still counts that line, restored to
       the declared encoding with the terminator. */
    if (tok->phantom_line != 0) {
        tok->buf[0] = '\0';
    }
    error->offset = (int32_t)(tok->cur - tok->buf);
    error->text = tok->buf;
    error->text_size = (size_t)(tok->inp - tok->buf);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_frontend_read_token(tinypy_tokenizer_t *tok, int32_t flags, tinypy_bool_t *started, tinypy_source_token_t *token) {
    tinypy_compile_ctx_t *ctx = tok->ctx;
    char *begin;
    char *end;

    if (ctx->limits.max_tokens != 0U && ctx->token_count >= ctx->limits.max_tokens) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    token->type = tinypy_internal_tokenizer_get(tok, &begin, &end);
    ctx->token_count += 1U;
    if (token->type == TINYPY_TOKEN_ERROR) {
        return tok->done;
    }
    if (token->type == TINYPY_TOKEN_END && *started != 0) {
        token->type = TINYPY_TOKEN_NEWLINE;
        *started = TINYPY_FALSE;
        if (tok->indent != 0 && (flags & TINYPY_PARSER_FLAG_DONT_IMPLY_DEDENT) == 0) {
            tok->pendin = -tok->indent;
            tok->indent = 0;
        }
    }
    else {
        *started = TINYPY_TRUE;
    }
    token->text = __tinypy_frontend_token_copy(ctx, begin, end);
    if (token->text == NULL) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    token->line = tok->line_number;
    token->column = begin != NULL && begin >= tok->line_start ? (int32_t)(begin - tok->line_start) : -1;
    token->position.buf = tok->buf;
    token->position.cur = tok->cur;
    token->position.inp = tok->inp;
    token->position.line_number = tok->line_number;
    token->position.done = tok->done;
    token->position.phantom_line = tok->phantom_line;
    return TINYPY_PARSER_OK;
}
//////////////////////////////////////////////////////////////////////////
/* A parser error is located where the tokenizer stopped after the failing
   token, so a tokenizer that has read past it returns there first. */
static void __tinypy_frontend_rewind(tinypy_tokenizer_t *tok, const tinypy_source_position_t *position) {
    tok->buf = position->buf;
    tok->cur = position->cur;
    tok->inp = position->inp;
    tok->line_number = position->line_number;
    tok->done = position->done;
    tok->phantom_line = position->phantom_line;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_frontend_token_is(const tinypy_source_token_t *token, tinypy_value_t *name) {
    size_t size = TINYPY_TEXT_BYTE_SIZE(name);
    tinypy_bool_t result = strlen(token->text) == size && memcmp(token->text, TINYPY_TEXT_BYTES(name), size) == 0;

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Reads the tokens after an @ that may spell the prefix @ meta . member.
   The prefix is lowered as tokens, never as source text: its name becomes
   an inaccessible marker until the meta pass consumes it, and an expression
   operation drops the @ while a declaration directive keeps the decorator
   shape. A failed read leaves the tokens before it to the parser. */
static int32_t __tinypy_frontend_meta_prefix(tinypy_tokenizer_t *tok, int32_t flags, tinypy_bool_t *started, tinypy_source_token_t *tokens, size_t *count, size_t *first) {
    static const int32_t expected[] = {TINYPY_TOKEN_NAME, TINYPY_TOKEN_DOT, TINYPY_TOKEN_NAME};
    tinypy_vm_t *vm = tok->ctx->vm;

    for (size_t index = 0U; index != sizeof(expected) / sizeof(expected[0]); ++index) {
        tinypy_source_token_t *token = &tokens[*count];
        int32_t result = __tinypy_frontend_read_token(tok, flags, started, token);

        if (result != TINYPY_PARSER_OK) {
            return result;
        }
        *count += 1U;
        if (token->type != expected[index]) {
            return TINYPY_PARSER_OK;
        }
        if (index == 0U && __tinypy_frontend_token_is(token, vm->internal_meta_name_key) == 0) {
            return TINYPY_PARSER_OK;
        }
    }
    const tinypy_source_token_t *member = &tokens[3];

    if (__tinypy_frontend_token_is(member, vm->internal_meta_template_key) == 0 && __tinypy_frontend_token_is(member, vm->internal_meta_emit_key) == 0 && __tinypy_frontend_token_is(member, vm->internal_meta_rename_key) == 0) {
        *first = 1U;
    }
    tinypy_value_t *marker = vm->internal_meta_namespace_key;
    const char *marker_text = (const char *)TINYPY_TEXT_BYTES(marker);

    tokens[1].text = __tinypy_frontend_token_copy(tok->ctx, marker_text, marker_text + TINYPY_TEXT_BYTE_SIZE(marker));
    if (tokens[1].text == NULL) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    tokens[1].line = tokens[0].line;
    tokens[1].column = tokens[0].column;
    tokens[1].position = tokens[0].position;
    return TINYPY_PARSER_OK;
}
//////////////////////////////////////////////////////////////////////////
tinypy_cst_node_t *tinypy_internal_parse_source(tinypy_compile_ctx_t *ctx, const char *source, size_t source_size, const char *filename, const tinypy_parser_grammar_t *g, int32_t start, tinypy_parser_error_detail_t *error, int32_t *flags) {
    tinypy_cst_node_t *result = NULL;
    tinypy_bool_t started = TINYPY_FALSE;

    __tinypy_frontend_init_error(error, filename);
    tinypy_tokenizer_t *tok = tinypy_internal_tokenizer_from_string(ctx, source, source_size, start == TINYPY_GRAMMAR_FILE_INPUT);
    if (tok == NULL) {
        error->result = TINYPY_PARSER_OUT_OF_MEMORY;
        return NULL;
    }
    tok->filename = filename;
    tok->alterror = (ctx->options.flags & (uint32_t)TINYPY_COMPILE_FLAG_TAB_ERROR) != 0U ? TINYPY_TRUE : TINYPY_FALSE;
    tok->altwarning = (ctx->options.flags & (uint32_t)TINYPY_COMPILE_FLAG_TAB_WARNING) != 0U ? TINYPY_TRUE : TINYPY_FALSE;
    tok->file_input = ctx->source_is_file;
    tok->ascii_lines = ctx->source_ascii_lines;
    tok->newline_faked = ctx->source_final_newline_added;
    tok->decode_error_line = ctx->source_decode_line;
    tok->decode_error_message = ctx->source_decode_message;
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
        tinypy_source_token_t tokens[4];
        size_t count = 1U;
        size_t first = 0U;
        int32_t read_result = TINYPY_PARSER_OK;

        error->result = __tinypy_frontend_read_token(tok, *flags, &started, &tokens[0]);
        if (error->result != TINYPY_PARSER_OK) {
            break;
        }
        if (tokens[0].type == TINYPY_TOKEN_AT && (ctx->options.feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_META) != 0U) {
            read_result = __tinypy_frontend_meta_prefix(tok, *flags, &started, tokens, &count, &first);
        }
        /* A limit refuses the source as a whole, while a source error read
           ahead is reported only after the parser accepts the tokens before
           it, as when each token is parsed once it is read. */
        if (read_result == TINYPY_PARSER_OUT_OF_MEMORY) {
            error->result = read_result;
            break;
        }
        for (size_t index = first; index != count; ++index) {
            tinypy_source_token_t *token = &tokens[index];

            error->result = tinypy_internal_parser_add_token(parser, token->type, token->text, token->line, token->column, &error->expected);
            if (error->result != TINYPY_PARSER_OK) {
                if (error->result != TINYPY_PARSER_DONE) {
                    error->token = token->type;
                    __tinypy_frontend_rewind(tok, &token->position);
                }
                break;
            }
        }
        if (error->result == TINYPY_PARSER_OK) {
            error->result = read_result;
        }
        if (error->result != TINYPY_PARSER_OK) {
            break;
        }
    }

    if (error->result == TINYPY_PARSER_DONE) {
        result = parser->tree;
        parser->tree = NULL;
    }
    *flags = (int32_t)parser->flags;
    if (result == NULL) {
        __tinypy_frontend_error_location(tok, error);
    }
    tinypy_internal_parser_release(parser);
    tinypy_internal_tokenizer_release(tok);
    return result;
}
