
/* Tokenizer implementation */

#include "value_ops.h"

#include "assertion.h"

#include "tokenizer.h"
#include "parser_error.h"

/* Don't ever change this -- it would break the portability of Python code */
#define TINYPY_TOKENIZER_TAB_SIZE 8

/* Forward */
static tinypy_tokenizer_t *__tok_new(tinypy_compile_ctx_t *ctx);
static int32_t __tok_nextc(tinypy_tokenizer_t *tok);
static void __tok_backup(tinypy_tokenizer_t *tok, int32_t c);

/* Token names */

const char *const __tinypy_parser_token_names[] = {
    "TINYPY_TOKEN_END",
    "TINYPY_TOKEN_NAME",
    "TINYPY_TOKEN_NUMBER",
    "TINYPY_TOKEN_STRING",
    "TINYPY_TOKEN_NEWLINE",
    "TINYPY_TOKEN_INDENT",
    "TINYPY_TOKEN_DEDENT",
    "TINYPY_TOKEN_LEFT_PARENTHESIS",
    "TINYPY_TOKEN_RIGHT_PARENTHESIS",
    "TINYPY_TOKEN_LEFT_BRACKET",
    "TINYPY_TOKEN_RIGHT_BRACKET",
    "TINYPY_TOKEN_COLON",
    "TINYPY_TOKEN_COMMA",
    "TINYPY_TOKEN_SEMICOLON",
    "TINYPY_TOKEN_PLUS",
    "TINYPY_TOKEN_MINUS",
    "TINYPY_TOKEN_STAR",
    "TINYPY_TOKEN_SLASH",
    "TINYPY_TOKEN_VERTICAL_BAR",
    "TINYPY_TOKEN_AMPERSAND",
    "TINYPY_TOKEN_LESS",
    "TINYPY_TOKEN_GREATER",
    "TINYPY_TOKEN_EQUAL",
    "TINYPY_TOKEN_DOT",
    "TINYPY_TOKEN_PERCENT",
    "TINYPY_TOKEN_BACKQUOTE",
    "TINYPY_TOKEN_LEFT_BRACE",
    "TINYPY_TOKEN_RIGHT_BRACE",
    "TINYPY_TOKEN_EQUAL_EQUAL",
    "TINYPY_TOKEN_NOT_EQUAL",
    "TINYPY_TOKEN_LESS_EQUAL",
    "TINYPY_TOKEN_GREATER_EQUAL",
    "TINYPY_TOKEN_TILDE",
    "TINYPY_TOKEN_CIRCUMFLEX",
    "TINYPY_TOKEN_LEFT_SHIFT",
    "TINYPY_TOKEN_RIGHT_SHIFT",
    "TINYPY_TOKEN_DOUBLE_STAR",
    "TINYPY_TOKEN_PLUS_EQUAL",
    "TINYPY_TOKEN_MINUS_EQUAL",
    "TINYPY_TOKEN_STAR_EQUAL",
    "TINYPY_TOKEN_SLASH_EQUAL",
    "TINYPY_TOKEN_PERCENT_EQUAL",
    "TINYPY_TOKEN_AMPERSAND_EQUAL",
    "TINYPY_TOKEN_VERTICAL_BAR_EQUAL",
    "TINYPY_TOKEN_CIRCUMFLEX_EQUAL",
    "TINYPY_TOKEN_LEFT_SHIFT_EQUAL",
    "TINYPY_TOKEN_RIGHT_SHIFT_EQUAL",
    "TINYPY_TOKEN_DOUBLE_STAR_EQUAL",
    "TINYPY_TOKEN_DOUBLE_SLASH",
    "TINYPY_TOKEN_DOUBLE_SLASH_EQUAL",
    "TINYPY_TOKEN_AT",
    /* This table must match the #defines in token.h! */
    "TINYPY_TOKEN_OPERATOR",
    "<TINYPY_TOKEN_ERROR>",
    "<TINYPY_TOKEN_COUNT>"};

/* Create and initialize a new tinypy_tokenizer_t structure */

//////////////////////////////////////////////////////////////////////////
static tinypy_tokenizer_t *__tok_new(tinypy_compile_ctx_t *ctx) {
    tinypy_tokenizer_t *tok = (tinypy_tokenizer_t *)tinypy_internal_compiler_arena_allocate(ctx, sizeof(tinypy_tokenizer_t));
    if (tok == NULL) {
        return NULL;
    }
    tok->buf = tok->cur = tok->end = tok->inp = tok->start = NULL;
    tok->ctx = ctx;
    tok->done = TINYPY_PARSER_OK;
    tok->tabsize = TINYPY_TOKENIZER_TAB_SIZE;
    tok->indent = 0;
    tok->indstack[0] = 0;
    tok->atbol = 1;
    tok->pendin = 0;
    tok->line_number = 0;
    tok->level = 0;
    tok->filename = NULL;
    tok->alterror = 0;
    tok->alttabsize = 1;
    tok->altindstack[0] = 0;
    tok->cont_line = 0;
    return tok;
}
//////////////////////////////////////////////////////////////////////////
static char *__new_string(tinypy_compile_ctx_t *ctx, const char *s, tinypy_compiler_size_t len) {
    char *result = (char *)tinypy_internal_compiler_arena_allocate(ctx, (size_t)len + 1U);
    if (result != NULL) {
        memcpy(result, s, len);
        result[len] = '\0';
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static char *__decode_str(const char *str, size_t source_size, int32_t exec_input, tinypy_tokenizer_t *tok) {
    (void)exec_input;
    TINYPY_ASSERT(source_size <= (size_t)PTRDIFF_MAX);
    return __new_string(tok->ctx, str, (tinypy_compiler_size_t)source_size);
}

/* Set up tokenizer for string */

//////////////////////////////////////////////////////////////////////////
tinypy_tokenizer_t *tinypy_internal_tokenizer_from_string(tinypy_compile_ctx_t *ctx, const char *str, size_t source_size, int32_t exec_input) {
    tinypy_tokenizer_t *tok = __tok_new(ctx);
    if (tok == NULL) {
        return NULL;
    }
    str = (char *)__decode_str(str, source_size, exec_input, tok);
    if (str == NULL) {
        tinypy_internal_tokenizer_release(tok);
        return NULL;
    }

    /* XXX: constify members. */
    tok->buf = tok->cur = tok->end = tok->inp = (char *)str;
    return tok;
}

/* Set up tokenizer for file */

/* Free a tinypy_tokenizer_t structure */

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tokenizer_release(tinypy_tokenizer_t *tok) {
    (void)tok;
}

/* Get next char, updating state; error code goes into tok->done */

//////////////////////////////////////////////////////////////////////////
static int32_t __tok_nextc(register tinypy_tokenizer_t *tok) {
    char *end;

    if (tok->cur != tok->inp) {
        return TINYPY_COMPILER_CHARMASK(*tok->cur++);
    }
    if (tok->done != TINYPY_PARSER_OK) {
        return TINYPY_TOKENIZER_END_OF_INPUT;
    }
    end = strchr(tok->inp, '\n');
    if (end != NULL) {
        end += 1;
    }
    else {
        end = strchr(tok->inp, '\0');
        if (end == tok->inp) {
            tok->done = TINYPY_PARSER_EOF;
            return TINYPY_TOKENIZER_END_OF_INPUT;
        }
    }
    if (tok->start == NULL) {
        tok->buf = tok->cur;
    }
    tok->line_start = tok->cur;
    tok->line_number += 1;
    tok->inp = end;
    return TINYPY_COMPILER_CHARMASK(*tok->cur++);
}

/* Back-up one character */

//////////////////////////////////////////////////////////////////////////
static void __tok_backup(register tinypy_tokenizer_t *tok, register int32_t c) {
    if (c != TINYPY_TOKENIZER_END_OF_INPUT) {
        tok->cur -= 1;
        TINYPY_ASSERT(tok->cur >= tok->buf);
        if (*tok->cur != c) {
            *tok->cur = c;
        }
    }
}

/* Return the token corresponding to a single character */

//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_token_one_character(int32_t c) {
    switch (c) {
    case '(':
        return TINYPY_TOKEN_LEFT_PARENTHESIS;
    case ')':
        return TINYPY_TOKEN_RIGHT_PARENTHESIS;
    case '[':
        return TINYPY_TOKEN_LEFT_BRACKET;
    case ']':
        return TINYPY_TOKEN_RIGHT_BRACKET;
    case ':':
        return TINYPY_TOKEN_COLON;
    case ',':
        return TINYPY_TOKEN_COMMA;
    case ';':
        return TINYPY_TOKEN_SEMICOLON;
    case '+':
        return TINYPY_TOKEN_PLUS;
    case '-':
        return TINYPY_TOKEN_MINUS;
    case '*':
        return TINYPY_TOKEN_STAR;
    case '/':
        return TINYPY_TOKEN_SLASH;
    case '|':
        return TINYPY_TOKEN_VERTICAL_BAR;
    case '&':
        return TINYPY_TOKEN_AMPERSAND;
    case '<':
        return TINYPY_TOKEN_LESS;
    case '>':
        return TINYPY_TOKEN_GREATER;
    case '=':
        return TINYPY_TOKEN_EQUAL;
    case '.':
        return TINYPY_TOKEN_DOT;
    case '%':
        return TINYPY_TOKEN_PERCENT;
    case '`':
        return TINYPY_TOKEN_BACKQUOTE;
    case '{':
        return TINYPY_TOKEN_LEFT_BRACE;
    case '}':
        return TINYPY_TOKEN_RIGHT_BRACE;
    case '^':
        return TINYPY_TOKEN_CIRCUMFLEX;
    case '~':
        return TINYPY_TOKEN_TILDE;
    case '@':
        return TINYPY_TOKEN_AT;
    default:
        return TINYPY_TOKEN_OPERATOR;
    }
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_token_two_characters(int32_t c1, int32_t c2) {
    switch (c1) {
    case '=':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_EQUAL_EQUAL;
        }
        break;
    case '!':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_NOT_EQUAL;
        }
        break;
    case '<':
        switch (c2) {
        case '>':
            return TINYPY_TOKEN_NOT_EQUAL;
        case '=':
            return TINYPY_TOKEN_LESS_EQUAL;
        case '<':
            return TINYPY_TOKEN_LEFT_SHIFT;
        }
        break;
    case '>':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_GREATER_EQUAL;
        case '>':
            return TINYPY_TOKEN_RIGHT_SHIFT;
        }
        break;
    case '+':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_PLUS_EQUAL;
        }
        break;
    case '-':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_MINUS_EQUAL;
        }
        break;
    case '*':
        switch (c2) {
        case '*':
            return TINYPY_TOKEN_DOUBLE_STAR;
        case '=':
            return TINYPY_TOKEN_STAR_EQUAL;
        }
        break;
    case '/':
        switch (c2) {
        case '/':
            return TINYPY_TOKEN_DOUBLE_SLASH;
        case '=':
            return TINYPY_TOKEN_SLASH_EQUAL;
        }
        break;
    case '|':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_VERTICAL_BAR_EQUAL;
        }
        break;
    case '%':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_PERCENT_EQUAL;
        }
        break;
    case '&':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_AMPERSAND_EQUAL;
        }
        break;
    case '^':
        switch (c2) {
        case '=':
            return TINYPY_TOKEN_CIRCUMFLEX_EQUAL;
        }
        break;
    }
    return TINYPY_TOKEN_OPERATOR;
}
//////////////////////////////////////////////////////////////////////////
int32_t __tinypy_token_three_characters(int32_t c1, int32_t c2, int32_t c3) {
    switch (c1) {
    case '<':
        switch (c2) {
        case '<':
            switch (c3) {
            case '=':
                return TINYPY_TOKEN_LEFT_SHIFT_EQUAL;
            }
            break;
        }
        break;
    case '>':
        switch (c2) {
        case '>':
            switch (c3) {
            case '=':
                return TINYPY_TOKEN_RIGHT_SHIFT_EQUAL;
            }
            break;
        }
        break;
    case '*':
        switch (c2) {
        case '*':
            switch (c3) {
            case '=':
                return TINYPY_TOKEN_DOUBLE_STAR_EQUAL;
            }
            break;
        }
        break;
    case '/':
        switch (c2) {
        case '/':
            switch (c3) {
            case '=':
                return TINYPY_TOKEN_DOUBLE_SLASH_EQUAL;
            }
            break;
        }
        break;
    }
    return TINYPY_TOKEN_OPERATOR;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __indenterror(tinypy_tokenizer_t *tok) {
    if (tok->alterror) {
        tok->done = TINYPY_PARSER_TAB_SPACE_ERROR;
        tok->cur = tok->inp;
        return 1;
    }
    return 0;
}

/* Get next token, after space stripping etc. */

//////////////////////////////////////////////////////////////////////////
static int32_t __tok_get(register tinypy_tokenizer_t *tok, char **p_start, char **p_end) {
    register int32_t c;
    int32_t blankline;

    *p_start = *p_end = NULL;
nextline:
    tok->start = NULL;
    blankline = 0;

    /* Get indentation level */
    if (tok->atbol) {
        register int32_t col = 0;
        register int32_t altcol = 0;
        tok->atbol = 0;
        for (;;) {
            c = __tok_nextc(tok);
            if (c == ' ') {
                col++, altcol++;
            }
            else if (c == '\t') {
                col = (col / tok->tabsize + 1) * tok->tabsize;
                altcol = (altcol / tok->alttabsize + 1) * tok->alttabsize;
            }
            else if (c == '\014') /* Control-L (formfeed) */ {
                col = altcol = 0;
            } /* For Emacs users */
            else {
                break;
            }
        }
        __tok_backup(tok, c);
        if (c == '#' || c == '\n') {
            /* Lines with only whitespace and/or comments
               shouldn't affect the indentation and are
               not passed to the parser as TINYPY_TOKEN_NEWLINE tokens,
               except *totally* empty lines in interactive
               mode, which signal the end of a command group. */
            blankline = 1;
            /* We can't jump back right here since we still
               may need to skip to the end of a comment */
        }
        if (!blankline && tok->level == 0) {
            if (col == tok->indstack[tok->indent]) {
                /* No change */
                if (altcol != tok->altindstack[tok->indent]) {
                    if (__indenterror(tok)) {
                        return TINYPY_TOKEN_ERROR;
                    }
                }
            }
            else if (col > tok->indstack[tok->indent]) {
                /* Indent -- always one */
                if (tok->indent + 1 >= TINYPY_TOKENIZER_MAX_INDENT) {
                    tok->done = TINYPY_PARSER_TOO_DEEP;
                    tok->cur = tok->inp;
                    return TINYPY_TOKEN_ERROR;
                }
                if (altcol <= tok->altindstack[tok->indent]) {
                    if (__indenterror(tok)) {
                        return TINYPY_TOKEN_ERROR;
                    }
                }
                tok->pendin++;
                tok->indstack[++tok->indent] = col;
                tok->altindstack[tok->indent] = altcol;
            }
            else /* col < tok->indstack[tok->indent] */ {
                /* Dedent -- any number, must be consistent */
                while (tok->indent > 0 && col < tok->indstack[tok->indent]) {
                    tok->pendin--;
                    tok->indent--;
                }
                if (col != tok->indstack[tok->indent]) {
                    tok->done = TINYPY_PARSER_DEDENT_ERROR;
                    tok->cur = tok->inp;
                    return TINYPY_TOKEN_ERROR;
                }
                if (altcol != tok->altindstack[tok->indent]) {
                    if (__indenterror(tok)) {
                        return TINYPY_TOKEN_ERROR;
                    }
                }
            }
        }
    }

    tok->start = tok->cur;

    /* Return pending indents/dedents */
    if (tok->pendin != 0) {
        if (tok->pendin < 0) {
            tok->pendin++;
            return TINYPY_TOKEN_DEDENT;
        }
        else {
            tok->pendin--;
            return TINYPY_TOKEN_INDENT;
        }
    }

again:
    tok->start = NULL;
    /* Skip spaces */
    do {
        c = __tok_nextc(tok);
    } while (c == ' ' || c == '\t' || c == '\014');

    /* Set start of current token */
    tok->start = tok->cur - 1;

    /* Skip comment, while looking for tab-setting magic */
    if (c == '#') {
        static const char *const tabforms[] = {
            "tab-width:",   /* Emacs */
            ":tabstop=",    /* vim, full form */
            ":ts=",         /* vim, abbreviated form */
            "set tabsize=", /* will vi never die? */
            /* more templates can be added here to support other editors */
        };
        char cbuf[80];
        char *tp;
        const char *const *cp;
        tp = cbuf;
        do {
            *tp++ = c = __tok_nextc(tok);
        } while (c != TINYPY_TOKENIZER_END_OF_INPUT && c != '\n' && (size_t)(tp - cbuf + 1) < sizeof(cbuf));
        *tp = '\0';
        for (cp = tabforms;
             cp < tabforms + sizeof(tabforms) / sizeof(tabforms[0]);
             cp++) {
            if ((tp = strstr(cbuf, *cp))) {
                int32_t newsize = atoi(tp + strlen(*cp));

                if (newsize >= 1 && newsize <= 40) {
                    tok->tabsize = newsize;
                }
            }
        }
        while (c != TINYPY_TOKENIZER_END_OF_INPUT && c != '\n') {
            c = __tok_nextc(tok);
        }
    }

    /* Check for end-of-input and errors now. */
    if (c == TINYPY_TOKENIZER_END_OF_INPUT) {
        return tok->done == TINYPY_PARSER_EOF ? TINYPY_TOKEN_END : TINYPY_TOKEN_ERROR;
    }

    /* Identifier (most frequent token!) */
    if (TINYPY_COMPILER_ISALPHA(c) || c == '_') {
        /* Process r"", u"" and ur"" */
        switch (c) {
        case 'b':
        case 'B':
            c = __tok_nextc(tok);
            if (c == 'r' || c == 'R') {
                c = __tok_nextc(tok);
            }
            if (c == '"' || c == '\'') {
                goto letter_quote;
            }
            break;
        case 'r':
        case 'R':
            c = __tok_nextc(tok);
            if (c == '"' || c == '\'') {
                goto letter_quote;
            }
            break;
        case 'u':
        case 'U':
            c = __tok_nextc(tok);
            if (c == 'r' || c == 'R') {
                c = __tok_nextc(tok);
            }
            if (c == '"' || c == '\'') {
                goto letter_quote;
            }
            break;
        }
        while (c != TINYPY_TOKENIZER_END_OF_INPUT && (TINYPY_COMPILER_ISALNUM(c) || c == '_')) {
            c = __tok_nextc(tok);
        }
        __tok_backup(tok, c);
        *p_start = tok->start;
        *p_end = tok->cur;
        return TINYPY_TOKEN_NAME;
    }

    /* Newline */
    if (c == '\n') {
        tok->atbol = 1;
        if (blankline || tok->level > 0) {
            goto nextline;
        }
        *p_start = tok->start;
        *p_end = tok->cur - 1; /* Leave '\n' out of the string */
        tok->cont_line = 0;
        return TINYPY_TOKEN_NEWLINE;
    }

    /* Period or number starting with period? */
    if (c == '.') {
        c = __tok_nextc(tok);
        if (isdigit(c)) {
            goto fraction;
        }
        else {
            __tok_backup(tok, c);
            *p_start = tok->start;
            *p_end = tok->cur;
            return TINYPY_TOKEN_DOT;
        }
    }

    /* Number */
    if (isdigit(c)) {
        if (c == '0') {
            /* Hex, octal or binary -- maybe. */
            c = __tok_nextc(tok);
            if (c == '.') {
                goto fraction;
            }
#ifndef WITHOUT_COMPLEX
            if (c == 'j' || c == 'J') {
                goto imaginary;
            }
#endif
            if (c == 'x' || c == 'X') {

                /* Hex */
                c = __tok_nextc(tok);
                if (!isxdigit(c)) {
                    tok->done = TINYPY_PARSER_BAD_TOKEN;
                    __tok_backup(tok, c);
                    return TINYPY_TOKEN_ERROR;
                }
                do {
                    c = __tok_nextc(tok);
                } while (isxdigit(c));
            }
            else if (c == 'o' || c == 'O') {
                /* Octal */
                c = __tok_nextc(tok);
                if (c < '0' || c >= '8') {
                    tok->done = TINYPY_PARSER_BAD_TOKEN;
                    __tok_backup(tok, c);
                    return TINYPY_TOKEN_ERROR;
                }
                do {
                    c = __tok_nextc(tok);
                } while ('0' <= c && c < '8');
            }
            else if (c == 'b' || c == 'B') {
                /* Binary */
                c = __tok_nextc(tok);
                if (c != '0' && c != '1') {
                    tok->done = TINYPY_PARSER_BAD_TOKEN;
                    __tok_backup(tok, c);
                    return TINYPY_TOKEN_ERROR;
                }
                do {
                    c = __tok_nextc(tok);
                } while (c == '0' || c == '1');
            }
            else {
                int32_t found_decimal = 0;
                /* Octal; c is first char of it */
                /* There's no 'isoctdigit' macro, sigh */
                while ('0' <= c && c < '8') {
                    c = __tok_nextc(tok);
                }
                if (isdigit(c)) {
                    found_decimal = 1;
                    do {
                        c = __tok_nextc(tok);
                    } while (isdigit(c));
                }
                if (c == '.') {
                    goto fraction;
                }
                else if (c == 'e' || c == 'E') {
                    goto exponent;
                }
#ifndef WITHOUT_COMPLEX
                else if (c == 'j' || c == 'J') {
                    goto imaginary;
                }
#endif
                else if (found_decimal) {
                    tok->done = TINYPY_PARSER_BAD_TOKEN;
                    __tok_backup(tok, c);
                    return TINYPY_TOKEN_ERROR;
                }
            }
            if (c == 'l' || c == 'L') {
                c = __tok_nextc(tok);
            }
        }
        else {
            /* Decimal */
            do {
                c = __tok_nextc(tok);
            } while (isdigit(c));
            if (c == 'l' || c == 'L') {
                c = __tok_nextc(tok);
            }
            else {
                /* Accept floating point numbers. */
                if (c == '.') {
                fraction:
                    /* Fraction */
                    do {
                        c = __tok_nextc(tok);
                    } while (isdigit(c));
                }
                if (c == 'e' || c == 'E') {
                    int32_t e;
                exponent:
                    e = c;
                    /* Exponent part */
                    c = __tok_nextc(tok);
                    if (c == '+' || c == '-') {
                        c = __tok_nextc(tok);
                        if (!isdigit(c)) {
                            tok->done = TINYPY_PARSER_BAD_TOKEN;
                            __tok_backup(tok, c);
                            return TINYPY_TOKEN_ERROR;
                        }
                    }
                    else if (!isdigit(c)) {
                        __tok_backup(tok, c);
                        __tok_backup(tok, e);
                        *p_start = tok->start;
                        *p_end = tok->cur;
                        return TINYPY_TOKEN_NUMBER;
                    }
                    do {
                        c = __tok_nextc(tok);
                    } while (isdigit(c));
                }
#ifndef WITHOUT_COMPLEX
                if (c == 'j' || c == 'J') {
                    /* Imaginary part */
                imaginary:
                    c = __tok_nextc(tok);
                }
#endif
            }
        }
        __tok_backup(tok, c);
        *p_start = tok->start;
        *p_end = tok->cur;
        return TINYPY_TOKEN_NUMBER;
    }

letter_quote:
    /* String */
    if (c == '\'' || c == '"') {
        tinypy_compiler_size_t quote2 = tok->cur - tok->start + 1;
        int32_t quote = c;
        int32_t triple = 0;
        int32_t tripcount = 0;
        for (;;) {
            c = __tok_nextc(tok);
            if (c == '\n') {
                if (!triple) {
                    tok->done = TINYPY_PARSER_EOL_STRING;
                    __tok_backup(tok, c);
                    return TINYPY_TOKEN_ERROR;
                }
                tripcount = 0;
                tok->cont_line = 1; /* multiline string. */
            }
            else if (c == TINYPY_TOKENIZER_END_OF_INPUT) {
                if (triple) {
                    tok->done = TINYPY_PARSER_EOF_TRIPLE_STRING;
                }
                else {
                    tok->done = TINYPY_PARSER_EOL_STRING;
                }
                tok->cur = tok->inp;
                return TINYPY_TOKEN_ERROR;
            }
            else if (c == quote) {
                tripcount++;
                if (tok->cur - tok->start == quote2) {
                    c = __tok_nextc(tok);
                    if (c == quote) {
                        triple = 1;
                        tripcount = 0;
                        continue;
                    }
                    __tok_backup(tok, c);
                }
                if (!triple || tripcount == 3) {
                    break;
                }
            }
            else if (c == '\\') {
                tripcount = 0;
                c = __tok_nextc(tok);
                if (c == TINYPY_TOKENIZER_END_OF_INPUT) {
                    tok->done = TINYPY_PARSER_EOL_STRING;
                    tok->cur = tok->inp;
                    return TINYPY_TOKEN_ERROR;
                }
            }
            else {
                tripcount = 0;
            }
        }
        *p_start = tok->start;
        *p_end = tok->cur;
        return TINYPY_TOKEN_STRING;
    }

    /* Line continuation */
    if (c == '\\') {
        c = __tok_nextc(tok);
        if (c != '\n') {
            tok->done = TINYPY_PARSER_LINE_CONTINUATION_ERROR;
            tok->cur = tok->inp;
            return TINYPY_TOKEN_ERROR;
        }
        tok->cont_line = 1;
        goto again; /* Read next line */
    }

    /* Check for two-character token */
    {
        int32_t c2 = __tok_nextc(tok);
        int32_t token = __tinypy_token_two_characters(c, c2);
        if (token != TINYPY_TOKEN_OPERATOR) {
            int32_t c3 = __tok_nextc(tok);
            int32_t token3 = __tinypy_token_three_characters(c, c2, c3);
            if (token3 != TINYPY_TOKEN_OPERATOR) {
                token = token3;
            }
            else {
                __tok_backup(tok, c3);
            }
            *p_start = tok->start;
            *p_end = tok->cur;
            return token;
        }
        __tok_backup(tok, c2);
    }

    /* Keep track of parentheses nesting level */
    switch (c) {
    case '(':
    case '[':
    case '{':
        tok->level++;
        break;
    case ')':
    case ']':
    case '}':
        tok->level--;
        break;
    }

    /* Punctuation character */
    *p_start = tok->start;
    *p_end = tok->cur;
    return __tinypy_token_one_character(c);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_tokenizer_get(tinypy_tokenizer_t *tok, char **p_start, char **p_end) {
    return __tok_get(tok, p_start, p_end);
}
