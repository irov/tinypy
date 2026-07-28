#ifndef TINYPY_COMPILER_TOKENIZER_H
#define TINYPY_COMPILER_TOKENIZER_H
#include "value_ops.h"

/* Tokenizer interface */

#include "token.h" /* For token types */

#define TINYPY_TOKENIZER_MAX_INDENT 100

/* Tokenizer state */
typedef struct tinypy_tokenizer_t {
    /* Input state; buf <= cur <= inp <= end */
    /* NB an entire line is held in the buffer */
    char *buf;
    char *cur;   /* Next character in buffer */
    char *inp;   /* End of data in buffer */
    char *end;   /* End of input buffer if buf != NULL */
    char *start; /* Start of current token if not NULL */
    int32_t done;    /* TINYPY_PARSER_OK normally, otherwise the tokenizer result. */
    /* NB If done != TINYPY_PARSER_OK, cur must be == inp!!! */
    tinypy_compile_ctx_t *ctx;
    int32_t tabsize; /* Tab spacing */
    int32_t indent;  /* Current indentation index */
    int32_t indstack[TINYPY_TOKENIZER_MAX_INDENT];
    tinypy_bool_t atbol; /* Nonzero if at begin of new line */
    int32_t pendin;      /* Pending indents (if > 0) or dedents (if < 0) */
    int32_t line_number; /* Current line number */
    int32_t level;       /* () [] {} Parentheses nesting level */
                     /* Used to allow free continuations inside them */
    /* Stuff for checking on different tab sizes */
    const char *filename; /* For error messages */
    tinypy_bool_t alterror;   /* Issue error if alternate tabs don't match */
    int32_t alttabsize;       /* Alternate tab spacing */
    int32_t altindstack[TINYPY_TOKENIZER_MAX_INDENT];
    tinypy_bool_t cont_line;    /* whether we are in a continuation line. */
    const char *line_start; /* pointer to start of current line */
} tinypy_tokenizer_t;

tinypy_tokenizer_t *tinypy_internal_tokenizer_from_string(tinypy_compile_ctx_t *ctx, const char *source, size_t source_size, tinypy_bool_t single_input);
void tinypy_internal_tokenizer_release(tinypy_tokenizer_t *tokenizer);
int32_t tinypy_internal_tokenizer_get(tinypy_tokenizer_t *tokenizer, char **out_start, char **out_end);
#endif /* !TINYPY_COMPILER_TOKENIZER_H */
