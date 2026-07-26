#ifndef TINYPY_COMPILER_PARSER_H
#define TINYPY_COMPILER_PARSER_H

#define TINYPY_PARSER_MAX_STACK 1500

typedef struct tinypy_parser_stack_entry_t {
    int32_t state_index;
    const tinypy_parser_dfa_t *rule;
    tinypy_cst_node_t *parent;
} tinypy_parser_stack_entry_t;

typedef struct tinypy_parser_stack_t {
    tinypy_parser_stack_entry_t *top;
    tinypy_parser_stack_entry_t entries[TINYPY_PARSER_MAX_STACK];
} tinypy_parser_stack_t;

typedef struct tinypy_parser_t {
    tinypy_parser_stack_t stack;
    const tinypy_parser_grammar_t *grammar;
    tinypy_cst_node_t *tree;
    tinypy_compile_ctx_t *context;
    uint32_t flags;
} tinypy_parser_t;

tinypy_parser_t *tinypy_internal_parser_new(tinypy_compile_ctx_t *ctx, const tinypy_parser_grammar_t *grammar, int32_t start_symbol);
void tinypy_internal_parser_release(tinypy_parser_t *parser);
int32_t tinypy_internal_parser_add_token(tinypy_parser_t *parser, int32_t type, char *text, int32_t line_number, int32_t column_offset, int32_t *out_expected);

#endif
