
/* Parser implementation */

/* For a description, see the comments at end of this file */

/* XXX To do: error recovery */

#include "value_ops.h"
#include "token.h"
#include "grammar.h"
#include "cst.h"
#include "parser.h"
#include "parser_error.h"

#define TINYPY_PARSER_TRACE(...) ((void)0)
#define TINYPY_PARSER_FUTURE_WITH_STATEMENT "with_statement"
#define TINYPY_PARSER_FUTURE_PRINT_FUNCTION "print_function"
#define TINYPY_PARSER_FUTURE_UNICODE_LITERALS "unicode_literals"

/* STACK DATA TYPE */

static void __tinypy_frontend_stack_reset(tinypy_parser_stack_t *);
static int __tinypy_frontend_stack_push(tinypy_parser_stack_t *, const tinypy_parser_dfa_t *, tinypy_cst_node_t *);
static void __tinypy_frontend_stack_pop(tinypy_parser_stack_t *);
static const tinypy_parser_dfa_t *__tinypy_frontend_find_dfa(const tinypy_parser_grammar_t *, int);
static int __tinypy_frontend_state_accepts(const tinypy_parser_dfa_state_t *);
static int __tinypy_frontend_state_transition(const tinypy_parser_grammar_t *, const tinypy_parser_dfa_state_t *, int, int *, const tinypy_parser_dfa_t **, int *);
static int __tinypy_frontend_state_expected(const tinypy_parser_grammar_t *, const tinypy_parser_dfa_state_t *);

//////////////////////////////////////////////////////////////////////////
static void __tinypy_frontend_stack_reset(tinypy_parser_stack_t *s) {
    s->top = &s->entries[TINYPY_PARSER_MAX_STACK];
}

#define TINYPY_PARSER_STACK_EMPTY(s) ((s)->top == &(s)->entries[TINYPY_PARSER_MAX_STACK])

//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_stack_push(register tinypy_parser_stack_t *s, const tinypy_parser_dfa_t *d, tinypy_cst_node_t *parent) {
    register tinypy_parser_stack_entry_t *top;
    size_t depth;

    depth = (size_t)(&s->entries[TINYPY_PARSER_MAX_STACK] - s->top);
    if (parent->context->limits.max_nesting != 0U && depth >= parent->context->limits.max_nesting) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    if (s->top == s->entries) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    top = --s->top;
    top->rule = d;
    top->parent = parent;
    top->state_index = 0;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_frontend_stack_pop(register tinypy_parser_stack_t *s) {
    assert(!TINYPY_PARSER_STACK_EMPTY(s));
    s->top++;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_parser_dfa_t *__tinypy_frontend_find_dfa(const tinypy_parser_grammar_t *g, int type) {
    int index = type - g->rules[0].symbol;

    assert(index >= 0 && index < g->rule_count);
    assert(g->rules[index].symbol == type);
    return &g->rules[index];
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_state_accepts(const tinypy_parser_dfa_state_t *s) {
    int index;

    for (index = 0; index < s->transition_count; index++) {
        if (s->transitions[index].label_index == TINYPY_GRAMMAR_EMPTY_LABEL) {
            return 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_state_expected(const tinypy_parser_grammar_t *g, const tinypy_parser_dfa_state_t *s) {
    int expected = -1;
    int index;

    for (index = 0; index < s->transition_count; index++) {
        const tinypy_parser_arc_t *candidate = &s->transitions[index];
        const tinypy_parser_label_t *candidate_label;

        if (candidate->label_index == TINYPY_GRAMMAR_EMPTY_LABEL) {
            continue;
        }
        candidate_label = &g->labels.items[candidate->label_index];
        if (!TINYPY_TOKEN_IS_TERMINAL(candidate_label->token_type)) {
            return -1;
        }
        if (expected != -1 && expected != candidate_label->token_type) {
            return -1;
        }
        expected = candidate_label->token_type;
    }
    return expected;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_state_transition(const tinypy_parser_grammar_t *g, const tinypy_parser_dfa_state_t *s, int ilabel, int *arrow, const tinypy_parser_dfa_t **push_dfa, int *push_type) {
    int index;

    *arrow = -1;
    *push_dfa = NULL;
    *push_type = 0;
    for (index = 0; index < s->transition_count; index++) {
        const tinypy_parser_arc_t *candidate = &s->transitions[index];
        const tinypy_parser_label_t *candidate_label;
        const tinypy_parser_dfa_t *nested;

        if (candidate->label_index == TINYPY_GRAMMAR_EMPTY_LABEL) {
            continue;
        }
        candidate_label = &g->labels.items[candidate->label_index];
        if (TINYPY_TOKEN_IS_TERMINAL(candidate_label->token_type)) {
            if (candidate->label_index != ilabel) {
                continue;
            }
            assert(*arrow == -1);
            *arrow = candidate->target_state;
            return 1;
        }
        nested = __tinypy_frontend_find_dfa(g, candidate_label->token_type);
        if (!TINYPY_BITSET_TEST(nested->first_set, ilabel)) {
            continue;
        }
        assert(*arrow == -1);
        *arrow = candidate->target_state;
        *push_dfa = nested;
        *push_type = candidate_label->token_type;
        return 1;
    }
    return 0;
}

/* PARSER CREATION */

//////////////////////////////////////////////////////////////////////////
tinypy_parser_t *tinypy_internal_parser_new(tinypy_compile_ctx_t *ctx, const tinypy_parser_grammar_t *g, int start) {
    tinypy_parser_t *ps;

    ps = (tinypy_parser_t *)tinypy_internal_compiler_arena_allocate(ctx, sizeof(tinypy_parser_t));
    if (ps == NULL) {
        return NULL;
    }
    ps->context = ctx;
    ps->grammar = g;
    ps->flags = 0;
    ps->tree = tinypy_internal_compiler_cst_new(ctx, start);
    if (ps->tree == NULL) {
        return NULL;
    }
    __tinypy_frontend_stack_reset(&ps->stack);
    const tinypy_parser_dfa_t *frontend_find_dfa = __tinypy_frontend_find_dfa(g, start);
    (void)__tinypy_frontend_stack_push(&ps->stack,
                                       frontend_find_dfa,
                                       ps->tree);
    return ps;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_parser_release(tinypy_parser_t *ps) {
    /* NB If you want to save the parse tree,
       you must set tree to NULL before calling delparser! */
    tinypy_internal_compiler_cst_release(ps->tree);
    (void)ps;
}

/* PARSER STACK OPERATIONS */

//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_shift(register tinypy_parser_stack_t *s, int type, char *str, int newstate, int lineno, int col_offset) {
    int err;
    assert(!TINYPY_PARSER_STACK_EMPTY(s));
    err = tinypy_internal_compiler_cst_add_child(s->top->parent, type, str, lineno, col_offset);
    if (err) {
        return err;
    }
    s->top->state_index = newstate;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_push(register tinypy_parser_stack_t *s, int type, const tinypy_parser_dfa_t *d, int newstate, int lineno, int col_offset) {
    int err;
    register tinypy_cst_node_t *n;
    n = s->top->parent;
    assert(!TINYPY_PARSER_STACK_EMPTY(s));
    err = tinypy_internal_compiler_cst_add_child(n, type, (char *)NULL, lineno, col_offset);
    if (err) {
        return err;
    }
    s->top->state_index = newstate;
    return __tinypy_frontend_stack_push(s, d, TINYPY_CST_CHILD(n, TINYPY_CST_CHILD_COUNT(n) - 1));
}

/* PARSER PROPER */

//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_keyword_label(const char *text) {
    switch (text[0]) {
    case 'a':
        if (strcmp(text, "and") == 0) {
            return 114;
        }
        if (strcmp(text, "as") == 0) {
            return 80;
        }
        if (strcmp(text, "assert") == 0) {
            return 86;
        }
        break;
    case 'b':
        if (strcmp(text, "break") == 0) {
            return 68;
        }
        break;
    case 'c':
        if (strcmp(text, "class") == 0) {
            return 161;
        }
        if (strcmp(text, "continue") == 0) {
            return 69;
        }
        break;
    case 'd':
        if (strcmp(text, "def") == 0) {
            return 20;
        }
        if (strcmp(text, "del") == 0) {
            return 60;
        }
        break;
    case 'e':
        if (strcmp(text, "elif") == 0) {
            return 93;
        }
        if (strcmp(text, "else") == 0) {
            return 94;
        }
        if (strcmp(text, "except") == 0) {
            return 102;
        }
        if (strcmp(text, "exec") == 0) {
            return 83;
        }
        break;
    case 'f':
        if (strcmp(text, "finally") == 0) {
            return 99;
        }
        if (strcmp(text, "for") == 0) {
            return 96;
        }
        if (strcmp(text, "from") == 0) {
            return 76;
        }
        break;
    case 'g':
        if (strcmp(text, "global") == 0) {
            return 82;
        }
        break;
    case 'i':
        if (strcmp(text, "if") == 0) {
            return 92;
        }
        if (strcmp(text, "import") == 0) {
            return 74;
        }
        if (strcmp(text, "in") == 0) {
            return 85;
        }
        if (strcmp(text, "is") == 0) {
            return 125;
        }
        break;
    case 'l':
        if (strcmp(text, "lambda") == 0) {
            return 109;
        }
        break;
    case 'n':
        if (strcmp(text, "not") == 0) {
            return 115;
        }
        break;
    case 'o':
        if (strcmp(text, "or") == 0) {
            return 112;
        }
        break;
    case 'p':
        if (strcmp(text, "pass") == 0) {
            return 62;
        }
        if (strcmp(text, "print") == 0) {
            return 58;
        }
        break;
    case 'r':
        if (strcmp(text, "raise") == 0) {
            return 71;
        }
        if (strcmp(text, "return") == 0) {
            return 70;
        }
        break;
    case 't':
        if (strcmp(text, "try") == 0) {
            return 97;
        }
        break;
    case 'w':
        if (strcmp(text, "while") == 0) {
            return 95;
        }
        if (strcmp(text, "with") == 0) {
            return 100;
        }
        break;
    case 'y':
        if (strcmp(text, "yield") == 0) {
            return 168;
        }
        break;
    default:
        break;
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_frontend_classify(tinypy_parser_t *ps, int type, char *str) {
    static const int16_t token_labels[TINYPY_TOKEN_COUNT] = {
        7, 21, 154, 155, 2, 103, 104, 13, 15, 146, 148, 23, 29, 34,
        135, 136, 30, 138, 127, 131, 118, 119, 27, 77, 139, 152, 149,
        151, 120, 124, 122, 121, 141, 129, 133, 59, 31, 46, 47, 48, 49,
        50, 51, 52, 53, 54, 55, 56, 140, 57, 11, -1, -1};

    if (type == TINYPY_TOKEN_NAME) {
        int keyword_label = __tinypy_frontend_keyword_label(str);

        if (keyword_label >= 0 && !(keyword_label == 58 && (ps->flags & TINYPY_CODE_FUTURE_PRINT_FUNCTION) != 0U)) {
            return keyword_label;
        }
    }
    if (type >= 0 && type < TINYPY_TOKEN_COUNT) {
        return token_labels[type];
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_frontend_future_hack(tinypy_parser_t *ps) {
    tinypy_cst_node_t *n = ps->stack.top->parent;
    tinypy_cst_node_t *ch, *cch;
    int i;

    /* from __future__ import ..., must have at least 4 children */
    n = TINYPY_CST_CHILD(n, 0);
    if (TINYPY_CST_CHILD_COUNT(n) < 4) {
        return;
    }
    ch = TINYPY_CST_CHILD(n, 0);
    if (TINYPY_CST_TEXT(ch) == NULL || strcmp(TINYPY_CST_TEXT(ch), "from") != 0) {
        return;
    }
    ch = TINYPY_CST_CHILD(n, 1);
    if (TINYPY_CST_CHILD_COUNT(ch) == 1 && TINYPY_CST_TEXT(TINYPY_CST_CHILD(ch, 0)) && strcmp(TINYPY_CST_TEXT(TINYPY_CST_CHILD(ch, 0)), "__future__") != 0) {
        return;
    }
    ch = TINYPY_CST_CHILD(n, 3);
    /* ch can be a star, a parenthesis or TINYPY_GRAMMAR_IMPORT_AS_NAMES */
    if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_STAR) {
        return;
    }
    if (TINYPY_CST_TYPE(ch) == TINYPY_TOKEN_LEFT_PARENTHESIS) {
        ch = TINYPY_CST_CHILD(n, 4);
    }

    for (i = 0; i < TINYPY_CST_CHILD_COUNT(ch); i += 2) {
        cch = TINYPY_CST_CHILD(ch, i);
        if (TINYPY_CST_CHILD_COUNT(cch) >= 1 && TINYPY_CST_TYPE(TINYPY_CST_CHILD(cch, 0)) == TINYPY_TOKEN_NAME) {
            char *str_ch = TINYPY_CST_TEXT(TINYPY_CST_CHILD(cch, 0));
            if (strcmp(str_ch, TINYPY_PARSER_FUTURE_WITH_STATEMENT) == 0) {
                ps->flags |= TINYPY_CODE_FUTURE_WITH_STATEMENT;
            }
            else if (strcmp(str_ch, TINYPY_PARSER_FUTURE_PRINT_FUNCTION) == 0) {
                ps->flags |= TINYPY_CODE_FUTURE_PRINT_FUNCTION;
            }
            else if (strcmp(str_ch, TINYPY_PARSER_FUTURE_UNICODE_LITERALS) == 0) {
                ps->flags |= TINYPY_CODE_FUTURE_UNICODE_LITERALS;
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
int tinypy_internal_parser_add_token(register tinypy_parser_t *ps, register int type, char *str, int lineno, int col_offset, int *expected_ret) {
    register int ilabel;
    int err;

    TINYPY_PARSER_TRACE("Token %s/'%s' ... ", __tinypy_parser_token_names[type], str);

    /* Find out which tinypy_parser_label_t this token is */
    ilabel = __tinypy_frontend_classify(ps, type, str);
    if (ilabel < 0) {
        return TINYPY_PARSER_SYNTAX_ERROR;
    }

    /* Loop until the token is shifted or an error occurred */
    for (;;) {
        /* Fetch the current tinypy_parser_dfa_t and tinypy_parser_dfa_state_t */
        register const tinypy_parser_dfa_t *d = ps->stack.top->rule;
        register const tinypy_parser_dfa_state_t *s = &d->states[ps->stack.top->state_index];

        TINYPY_PARSER_TRACE(" DFA '%s', tinypy_parser_dfa_state_t %d:",
                            d->name, ps->stack.top->state_index);

        {
            int arrow;
            int push_type;
            const tinypy_parser_dfa_t *push_dfa;

            if (__tinypy_frontend_state_transition(ps->grammar, s, ilabel,
                                                   &arrow, &push_dfa,
                                                   &push_type)) {
                if (push_dfa != NULL) {
                    if ((err = __tinypy_frontend_push(&ps->stack, push_type,
                                                      push_dfa, arrow, lineno,
                                                      col_offset)) > 0) {
                        TINYPY_PARSER_TRACE(" MemError: push\n");
                        return err;
                    }
                    TINYPY_PARSER_TRACE(" Push ...\n");
                    continue;
                }

                /* Shift the token */
                if ((err = __tinypy_frontend_shift(&ps->stack, type, str,
                                                   arrow, lineno,
                                                   col_offset)) > 0) {
                    TINYPY_PARSER_TRACE(" MemError: shift.\n");
                    return err;
                }
                TINYPY_PARSER_TRACE(" Shift.\n");
                /* Pop while we are in an accept-only tinypy_parser_dfa_state_t */
                while ((s = &d->states
                                 [ps->stack.top->state_index]),
                       __tinypy_frontend_state_accepts(s) && s->transition_count == 1) {
                    TINYPY_PARSER_TRACE("  DFA '%s', tinypy_parser_dfa_state_t %d: "
                                        "Direct pop.\n",
                                        d->name,
                                        ps->stack.top->state_index);
                    if (d->name[0] == 'i' && strcmp(d->name,
                               "import_stmt") == 0) {
                        __tinypy_frontend_future_hack(ps);
                    }
                    __tinypy_frontend_stack_pop(&ps->stack);
                    if (TINYPY_PARSER_STACK_EMPTY(&ps->stack)) {
                        TINYPY_PARSER_TRACE("  ACCEPT.\n");
                        return TINYPY_PARSER_DONE;
                    }
                    d = ps->stack.top->rule;
                }
                return TINYPY_PARSER_OK;
            }
        }

        if (__tinypy_frontend_state_accepts(s)) {
            if (d->name[0] == 'i' && strcmp(d->name, "import_stmt") == 0) {
                __tinypy_frontend_future_hack(ps);
            }
            /* Pop this tinypy_parser_dfa_t and try again */
            __tinypy_frontend_stack_pop(&ps->stack);
            TINYPY_PARSER_TRACE(" Pop ...\n");
            if (TINYPY_PARSER_STACK_EMPTY(&ps->stack)) {
                TINYPY_PARSER_TRACE(" Error: bottom of tinypy_parser_stack_t.\n");
                return TINYPY_PARSER_SYNTAX_ERROR;
            }
            continue;
        }

        /* Stuck, report syntax error */
        TINYPY_PARSER_TRACE(" Error.\n");
        if (expected_ret) {
            *expected_ret = __tinypy_frontend_state_expected(ps->grammar, s);
        }
        return TINYPY_PARSER_SYNTAX_ERROR;
    }
}

/*

Description
-----------

The parser's interface is different than usual: the function addtoken()
must be called for each token in the input.  This makes it possible to
turn it into an incremental parsing system later.  The parsing system
constructs a parse tree as it goes.

A parsing rule is represented as a Deterministic Finite-tinypy_parser_dfa_state_t Automaton
(DFA).  A tinypy_cst_node_t in a DFA represents a tinypy_parser_dfa_state_t of the parser; an tinypy_parser_arc_t represents
a transition.  Transitions are either labeled with terminal symbols or
with non-terminals.  When the parser decides to follow an tinypy_parser_arc_t labeled
with a non-terminal, it is invoked recursively with the DFA representing
the parsing rule for that as its initial tinypy_parser_dfa_state_t; when that DFA accepts,
the parser that invoked it continues.  The parse tree constructed by the
recursively called parser is inserted as a child in the current parse tree.

The DFA's can be constructed automatically from a more conventional
language description.  An extended LL(1) tinypy_parser_grammar_t (ELL(1)) is suitable.
Certain restrictions make the parser's life easier: rules that can produce
the empty string should be outlawed (there are other ways to put loops
or optional parts in the language).  To avoid the need to construct
FIRST sets, we can require that all but the last alternative of a rule
(really: tinypy_parser_arc_t going out of a DFA's tinypy_parser_dfa_state_t) must begin with a terminal
symbol.

As an example, consider this tinypy_parser_grammar_t:

TINYPY_GRAMMAR_EXPR:   TINYPY_GRAMMAR_TERM (TINYPY_TOKEN_OPERATOR TINYPY_GRAMMAR_TERM)*
TINYPY_GRAMMAR_TERM:   CONSTANT | '(' TINYPY_GRAMMAR_EXPR ')'

The DFA corresponding to the rule for TINYPY_GRAMMAR_EXPR is:

------->.---TINYPY_GRAMMAR_TERM-->.------->
    ^          |
    |          |
    \----TINYPY_TOKEN_OPERATOR----/

The parse tree generated for the input a+b is:

(TINYPY_GRAMMAR_EXPR: (TINYPY_GRAMMAR_TERM: (TINYPY_TOKEN_NAME: a)), (TINYPY_TOKEN_OPERATOR: +), (TINYPY_GRAMMAR_TERM: (TINYPY_TOKEN_NAME: b)))

*/
